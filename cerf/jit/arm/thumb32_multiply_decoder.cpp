#include "thumb32_multiply_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "decoded_insn.h"
#include "place_fns.h"
#include "thumb32_fatal.h"

REGISTER_SERVICE(Thumb32MultiplyDecoder);

bool Thumb32MultiplyDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void Thumb32MultiplyDecoder::OnReady() {
    fatal_ = &emu_.Get<Thumb32Fatal>();
}

/* DDI 0406C.c A8.8.114 MUL encoding T2 (p. A8-502) and A8.8.100 MLA encoding
   T1 (p. A8-480): "if Ra == '1111' then SEE MUL", "setflags = FALSE", "if
   d IN {13,15} || n IN {13,15} || m IN {13,15} || a == 13 then
   UNPREDICTABLE". Opcodes are the ARM numbering of Table A5-7 (p. A5-202). */
bool Thumb32MultiplyDecoder::DecodeMultiplyAccumulate(DecodedInsn* insn,
                                                      uint32_t op) {
    const uint32_t rn = (op >> 16) & 0xFu;
    const uint32_t ra = (op >> 12) & 0xFu;
    const uint32_t rd = (op >>  8) & 0xFu;
    const uint32_t rm =  op        & 0xFu;

    if (rd == 13u || rd == 0xFu || rn == 13u || rn == 0xFu ||
        rm == 13u || rm == 0xFu || ra == 13u) {
        return false;
    }

    insn->op1      = ra == 0xFu ? 0u : 1u;
    insn->s        = 0u;
    insn->rd       = rd;
    insn->rn       = rn;
    insn->rm       = rm;
    insn->rs       = ra == 0xFu ? 0u : ra;
    insn->place_fn = &PlaceMultiply;
    return true;
}

/* DDI 0406C.c A8.8.101 MLS encoding T1 (p. A8-482): "if d IN {13,15} ||
   n IN {13,15} || m IN {13,15} || a IN {13,15} then UNPREDICTABLE". */
bool Thumb32MultiplyDecoder::DecodeMultiplySubtract(DecodedInsn* insn,
                                                    uint32_t op) {
    const uint32_t rn = (op >> 16) & 0xFu;
    const uint32_t ra = (op >> 12) & 0xFu;
    const uint32_t rd = (op >>  8) & 0xFu;
    const uint32_t rm =  op        & 0xFu;

    if (rd == 13u || rd == 0xFu || rn == 13u || rn == 0xFu ||
        rm == 13u || rm == 0xFu || ra == 13u || ra == 0xFu) {
        return false;
    }

    insn->op1      = 3u;
    insn->s        = 0u;
    insn->rd       = rd;
    insn->rn       = rn;
    insn->rm       = rm;
    insn->rs       = ra;
    insn->place_fn = &PlaceMultiply;
    return true;
}

/* DDI 0406C.c A8.8.176 SMLABB/BT/TB/TT T1 (p. A8-620) and A8.8.188
   SMULBB/BT/TB/TT T1 (p. A8-644): "if Ra == '1111' then SEE SMULBB ...";
   "n_high = (N == '1'); m_high = (M == '1')" with N = hw2[5], M = hw2[4];
   d/n/m IN {13,15} and a == 13 UNPREDICTABLE. Table A5-9 (p. A5-203) rows. */
bool Thumb32MultiplyDecoder::DecodeHalfwordMultiply(DecodedInsn* insn,
                                                    uint32_t op) {
    const uint32_t rn = (op >> 16) & 0xFu;
    const uint32_t ra = (op >> 12) & 0xFu;
    const uint32_t rd = (op >>  8) & 0xFu;
    const uint32_t rm =  op        & 0xFu;

    if (rd == 13u || rd == 0xFu || rn == 13u || rn == 0xFu ||
        rm == 13u || rm == 0xFu || ra == 13u) {
        return false;
    }

    insn->op1      = ra == 0xFu ? 3u : 0u;
    insn->n        = (op >> 5) & 0x1u;
    insn->u        = (op >> 4) & 0x1u;
    insn->rd       = rd;
    insn->rn       = rn;
    insn->rm       = rm;
    insn->rs       = ra == 0xFu ? 0u : ra;
    insn->place_fn = &PlaceHalfwordMultiply;
    return true;
}

/* DDI 0406C.c A8.8.181 SMLAWB, SMLAWT encoding T1 (p. A8-630): "if Ra ==
   '1111' then SEE SMULWB, SMULWT", "m_high = (M == '1')" with M = hw2[4],
   "if d IN {13,15} || n IN {13,15} || m IN {13,15} || a == 13 then
   UNPREDICTABLE". A8.8.190 (p. A8-648) drops the Ra clauses. */
bool Thumb32MultiplyDecoder::DecodeWordByHalfwordMultiply(DecodedInsn* insn,
                                                          uint32_t op) {
    const uint32_t rn = (op >> 16) & 0xFu;
    const uint32_t ra = (op >> 12) & 0xFu;
    const uint32_t rd = (op >>  8) & 0xFu;
    const uint32_t rm =  op        & 0xFu;

    if (rd == 13u || rd == 0xFu || rn == 13u || rn == 0xFu ||
        rm == 13u || rm == 0xFu || ra == 13u) {
        return false;
    }

    insn->op1      = 1u;
    insn->s        = ra == 0xFu ? 1u : 0u;
    insn->u        = (op >> 4) & 0x1u;
    insn->rd       = rd;
    insn->rn       = rn;
    insn->rm       = rm;
    insn->rs       = ra == 0xFu ? 0u : ra;
    insn->place_fn = &PlaceHalfwordMultiply;
    return true;
}

/* DDI 0406C.c A6.3.16 Table A6-28 (p. A6-249): op1 = bits[22:20], Rn =
   bits[19:16], Ra = bits[15:12], Rd = bits[11:8], op2 = bits[5:4], Rm =
   bits[3:0]. "If, in the second halfword of the instruction, bits[7:6] !=
   0b00, the instruction is UNDEFINED"; other encodings are UNDEFINED too. */
bool Thumb32MultiplyDecoder::DecodeMultiplyAbsoluteDifference(DecodedInsn* insn,
                                                              uint32_t op) {
    if (((op >> 6) & 0x3u) != 0u) {
        return false;
    }

    const uint32_t op1 = (op >> 20) & 0x7u;
    const uint32_t op2 = (op >>  4) & 0x3u;

    switch (op1) {
    case 0x0u:
        if (op2 == 0x0u) return DecodeMultiplyAccumulate(insn, op);
        if (op2 == 0x1u) return DecodeMultiplySubtract(insn, op);
        return false;
    case 0x1u:
        return DecodeHalfwordMultiply(insn, op);
    case 0x3u:
        if ((op2 & 0x2u) != 0u) return false;
        return DecodeWordByHalfwordMultiply(insn, op);
    case 0x2u:
        if ((op2 & 0x2u) != 0u) return false;
        fatal_->Unimplemented("signed multiply accumulate dual (A6-249)",
                              insn, op);
    case 0x4u:
        if ((op2 & 0x2u) != 0u) return false;
        fatal_->Unimplemented("signed multiply subtract dual (A6-249)",
                              insn, op);
    case 0x5u:
        if ((op2 & 0x2u) != 0u) return false;
        fatal_->Unimplemented("signed most significant word multiply "
                              "accumulate (A6-249)", insn, op);
    case 0x6u:
        if ((op2 & 0x2u) != 0u) return false;
        fatal_->Unimplemented("signed most significant word multiply "
                              "subtract (A6-249)", insn, op);
    default:
        if (op2 != 0x0u) return false;
        fatal_->Unimplemented("unsigned sum of absolute differences (A6-249)",
                              insn, op);
    }
}
