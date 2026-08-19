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
    if ((op2 & 0x1u) == 0u) {
        fatal_->Unimplemented("store register dual (A6-238)", insn, op);
    }
    fatal_->Unimplemented(((op >> 16) & 0xFu) == 0xFu
                              ? "load register dual, literal (A6-238)"
                              : "load register dual, immediate (A6-238)",
                          insn, op);
}
