#include "thumb32_plain_imm_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "place_fns.h"
#include "thumb32_fatal.h"

REGISTER_SERVICE(Thumb32PlainImmDecoder);

bool Thumb32PlainImmDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void Thumb32PlainImmDecoder::OnReady() {
    fatal_ = &emu_.Get<Thumb32Fatal>();
}

/* DDI 0406C.c A8.8.4 ADD (imm, Thumb) T4 p. A8-306, A8.8.221 SUB T4 p. A8-708;
   SEE redirects A8.8.12 ADR T3/T2 p. A8-322 (Operation p. A8-323), A8.8.9 ADD
   (SP plus imm) T4 p. A8-316, A8.8.225 SUB (SP minus imm) T3 p. A8-716;
   Align(PC,4) per A4.2.2 p. A4-162. */
bool Thumb32PlainImmDecoder::DecodeAddSubImm12(DecodedInsn* insn, uint32_t op,
                                               bool add) {
    const uint32_t rn    = (op >> 16) & 0xFu;
    const uint32_t rd    = (op >>  8) & 0xFu;
    const uint32_t imm32 = (((op >> 26) & 0x1u) << 11) |
                           (((op >> 12) & 0x7u) << 8) | (op & 0xFFu);

    if (rn == 0xFu) {
        if (rd == 13u || rd == 0xFu) {
            return false;
        }
        const uint32_t base = (insn->guest_address + 4u) & ~3u;
        insn->rd        = rd;
        insn->immediate = add ? base + imm32 : base - imm32;
        insn->place_fn  = &PlaceMovw;
        return true;
    }

    const bool sp_form = rn == 13u;
    if (rd == 0xFu || (rd == 13u && !sp_form)) {
        return false;
    }
    insn->op1       = add ? 4u : 2u;
    insn->s         = 0u;
    insn->rn        = rn;
    insn->rd        = rd;
    insn->immediate = imm32;
    insn->rs        = 0u;
    insn->place_fn  = &PlaceDataProcessing;
    return true;
}

/* DDI 0406C.c A8.8.102 MOV (immediate) T3 p. A8-484, A8.8.106 MOVT T1
   p. A8-491; imm4 occupies the Rn field at bits[19:16]. */
bool Thumb32PlainImmDecoder::DecodeMoveImm16(DecodedInsn* insn, uint32_t op,
                                             ArmPlaceFn place) {
    const uint32_t rd = (op >> 8) & 0xFu;
    if (rd == 13u || rd == 0xFu) {
        return false;
    }
    insn->rd        = rd;
    insn->immediate = (((op >> 16) & 0xFu) << 12) | (((op >> 26) & 0x1u) << 11) |
                      (((op >> 12) & 0x7u) << 8) | (op & 0xFFu);
    insn->place_fn  = place;
    return true;
}

/* DDI 0406C.c A8.8.164 SBFX T1 p. A8-598, A8.8.246 UBFX T1 p. A8-756;
   widthm1 at hw2[4:0], bit[26] and hw2[5] are (0) per A6.1.1 p. A6-220. */
bool Thumb32PlainImmDecoder::DecodeBitFieldExtract(DecodedInsn* insn,
                                                   uint32_t op,
                                                   ArmPlaceFn place) {
    if (((op >> 26) & 0x1u) != 0u || ((op >> 5) & 0x1u) != 0u) {
        return false;
    }
    const uint32_t rn    = (op >> 16) & 0xFu;
    const uint32_t rd    = (op >>  8) & 0xFu;
    const uint32_t lsb   = (((op >> 12) & 0x7u) << 2) | ((op >> 6) & 0x3u);
    const uint32_t width = (op & 0x1Fu) + 1u;
    if (rd == 13u || rd == 0xFu || rn == 13u || rn == 0xFu) {
        return false;
    }
    if (lsb + width > 32u) {
        return false;
    }
    insn->rd       = rd;
    insn->rn       = rn;
    insn->op1      = lsb;
    insn->rs       = width;
    insn->place_fn = place;
    return true;
}

/* DDI 0406C.c A8.8.20 BFI T1 p. A8-338, A8.8.19 BFC T1 p. A8-336 (Rn == 1111);
   msb at hw2[4:0], bit[26] and hw2[5] are (0) per A6.1.1 p. A6-220. */
bool Thumb32PlainImmDecoder::DecodeBitFieldInsert(DecodedInsn* insn,
                                                  uint32_t op) {
    if (((op >> 26) & 0x1u) != 0u || ((op >> 5) & 0x1u) != 0u) {
        return false;
    }
    const uint32_t rn  = (op >> 16) & 0xFu;
    const uint32_t rd  = (op >>  8) & 0xFu;
    const uint32_t lsb = (((op >> 12) & 0x7u) << 2) | ((op >> 6) & 0x3u);
    const uint32_t msb =  op        & 0x1Fu;
    if (rd == 13u || rd == 0xFu || rn == 13u || msb < lsb) {
        return false;
    }
    const uint32_t width = msb - lsb + 1u;
    insn->rd        = rd;
    insn->rn        = rn;
    insn->op1       = lsb;
    insn->rs        = width;
    insn->immediate = (width == 32u) ? 0xFFFFFFFFu
                                     : (((1u << width) - 1u) << lsb);
    insn->place_fn  = (rn == 0xFu) ? &PlaceBfc : &PlaceBfi;
    return true;
}

/* DDI 0406C.c A8.8.193 SSAT T1 p. A8-652, A8.8.266 USAT T1 p. A8-796; the
   imm3:imm2 == 0 SEE redirects are A8.8.194 SSAT16 p. A8-654 and A8.8.267
   USAT16 p. A8-798. */
bool Thumb32PlainImmDecoder::DecodeSaturate(DecodedInsn* insn, uint32_t op) {
    const uint32_t o     = (op >> 20) & 0x1Fu;
    const uint32_t shift = (((op >> 12) & 0x7u) << 2) | ((op >> 6) & 0x3u);
    const bool sixteen   = (o == 0x12u || o == 0x1Au) && shift == 0u;
    const bool is_signed = o == 0x10u || o == 0x12u;
    fatal_->Unimplemented(
        is_signed ? (sixteen ? "Signed Saturate, two 16-bit (A6-234)"
                             : "Signed Saturate (A6-234)")
                  : (sixteen ? "Unsigned Saturate, two 16-bit (A6-234)"
                             : "Unsigned Saturate (A6-234)"),
        insn, op);
}

/* DDI 0406C.c A6.3.3 and Table A6-12 p. A6-234: op = bits[24:20],
   Rn = bits[19:16]; other encodings in this space are UNDEFINED. */
bool Thumb32PlainImmDecoder::Decode(DecodedInsn* insn, uint32_t op) {
    switch ((op >> 20) & 0x1Fu) {
    case 0x00u: return DecodeAddSubImm12(insn, op, true);
    case 0x0Au: return DecodeAddSubImm12(insn, op, false);
    case 0x04u: return DecodeMoveImm16(insn, op, &PlaceMovw);
    case 0x0Cu: return DecodeMoveImm16(insn, op, &PlaceMovt);
    case 0x14u: return DecodeBitFieldExtract(insn, op, &PlaceSbfx);
    case 0x1Cu: return DecodeBitFieldExtract(insn, op, &PlaceUbfx);
    case 0x16u: return DecodeBitFieldInsert(insn, op);
    case 0x10u:
    case 0x12u:
    case 0x18u:
    case 0x1Au: return DecodeSaturate(insn, op);
    default:    return false;
    }
}
