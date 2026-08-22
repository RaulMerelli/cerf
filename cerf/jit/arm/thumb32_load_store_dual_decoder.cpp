#include "thumb32_load_store_dual_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "decoded_insn.h"
#include "place_fns.h"
#include "thumb32_fatal.h"

REGISTER_SERVICE(Thumb32LoadStoreDualDecoder);

bool Thumb32LoadStoreDualDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void Thumb32LoadStoreDualDecoder::OnReady() {
    fatal_ = &emu_.Get<Thumb32Fatal>();
}

/* DDI 0406C.c A8.8.236 TBB, TBH encoding T1 (p. A8-736): Rn = bits[19:16],
   Rm = hw2[3:0], "if n == 13 || m IN {13,15} then UNPREDICTABLE". hw2[15:12]
   are (1) and hw2[11:8] are (0); A6.1.1 (p. A6-220) makes a (1) bit that is
   not 1, or a (0) bit that is not 0, UNPREDICTABLE. */
bool Thumb32LoadStoreDualDecoder::DecodeTableBranchByte(DecodedInsn* insn,
                                                        uint32_t op) {
    const uint32_t rn = (op >> 16) & 0xFu;
    const uint32_t rm =  op        & 0xFu;

    if (((op >> 12) & 0xFu) != 0xFu || ((op >> 8) & 0xFu) != 0u) {
        return false;
    }
    if (rn == 13u || rm == 13u || rm == 0xFu) {
        return false;
    }

    insn->rn           = rn;
    insn->rm           = rm;
    insn->r15_modified = true;
    insn->place_fn     = &PlaceTableBranchByte;
    return true;
}

/* DDI 0406C.c Table A6-17 (p. A6-238), the op1 = 01 rows keyed on
   op3 = hw2[7:4]. */
bool Thumb32LoadStoreDualDecoder::DecodeExclusiveOrTableBranch(
    DecodedInsn* insn, uint32_t op) {
    const uint32_t op2 = (op >> 20) & 0x3u;
    const uint32_t op3 = (op >>  4) & 0xFu;

    if (op2 == 0x0u) {
        switch (op3) {
        case 0x4u:
            fatal_->Unimplemented("store register exclusive byte (A6-238)",
                                  insn, op);
        case 0x5u:
            fatal_->Unimplemented("store register exclusive halfword (A6-238)",
                                  insn, op);
        case 0x7u:
            fatal_->Unimplemented(
                "store register exclusive doubleword (A6-238)", insn, op);
        default:
            return false;
        }
    }

    switch (op3) {
    case 0x0u:
        return DecodeTableBranchByte(insn, op);
    case 0x1u:
        fatal_->Unimplemented("table branch halfword (A6-238)", insn, op);
    case 0x4u:
        fatal_->Unimplemented("load register exclusive byte (A6-238)", insn,
                              op);
    case 0x5u:
        fatal_->Unimplemented("load register exclusive halfword (A6-238)",
                              insn, op);
    case 0x7u:
        fatal_->Unimplemented("load register exclusive doubleword (A6-238)",
                              insn, op);
    default:
        return false;
    }
}

/* DDI 0406C.c A8.8.72 LDRD (immediate) T1 (p. A8-426), A8.8.73 LDRD (literal)
   T1 (p. A8-428), A8.8.210 STRD (immediate) T1 (p. A8-686): "t = UInt(Rt);
   t2 = UInt(Rt2); imm32 = ZeroExtend(imm8:'00', 32); add = (U == '1')". The
   immediate forms add "n = UInt(Rn); index = (P == '1'); wback = (W == '1')". */
bool Thumb32LoadStoreDualDecoder::DecodeDual(DecodedInsn* insn, uint32_t op) {
    const uint32_t p    = (op >> 24) & 0x1u;
    const uint32_t u    = (op >> 23) & 0x1u;
    const uint32_t w    = (op >> 21) & 0x1u;
    const bool     load = ((op >> 20) & 0x1u) != 0u;
    const uint32_t rn   = (op >> 16) & 0xFu;
    const uint32_t rt   = (op >> 12) & 0xFu;
    const uint32_t rt2  = (op >>  8) & 0xFu;

    /* "if t IN {13,15} || t2 IN {13,15} then UNPREDICTABLE" on all three;
       "|| t == t2" on the two loads; STRD adds "if n == 15"; the literal adds
       "if W == '1' then UNPREDICTABLE". */
    if (rt == 13u || rt == 15u || rt2 == 13u || rt2 == 15u) {
        return false;
    }
    if (load) {
        if (rt == rt2 || (rn == 15u && w != 0u)) {
            return false;
        }
    } else if (rn == 15u) {
        return false;
    }
    /* "if wback && (n == t || n == t2) then UNPREDICTABLE". */
    if (w != 0u && (rn == rt || rn == rt2)) {
        return false;
    }

    const int32_t imm32 = static_cast<int32_t>((op & 0xFFu) * 4u);
    insn->op1      = load ? 2u : 3u;
    insn->l        = 0u;
    insn->n        = 1u;
    insn->p        = p;
    insn->u        = u;
    insn->w        = w;
    insn->rn       = rn;
    insn->rd       = rt;
    insn->rd2      = rt2;
    insn->offset   = u != 0u ? imm32 : -imm32;
    insn->place_fn = &PlaceLoadStoreExtension;
    return true;
}

/* DDI 0406C.c A6.3.6 and Table A6-17 (p. A6-238): op1 = bits[24:23],
   op2 = bits[21:20], op3 = hw2[7:4], Rn = bits[19:16]; "Other encodings in
   this space are UNDEFINED". */
bool Thumb32LoadStoreDualDecoder::Decode(DecodedInsn* insn, uint32_t op) {
    const uint32_t op1 = (op >> 23) & 0x3u;
    const uint32_t op2 = (op >> 20) & 0x3u;

    if (op1 == 0x0u && op2 == 0x0u) {
        fatal_->Unimplemented("store register exclusive (A6-238)", insn, op);
    }
    if (op1 == 0x0u && op2 == 0x1u) {
        fatal_->Unimplemented("load register exclusive (A6-238)", insn, op);
    }
    if (op1 == 0x1u && op2 <= 0x1u) {
        return DecodeExclusiveOrTableBranch(insn, op);
    }
    return DecodeDual(insn, op);
}
