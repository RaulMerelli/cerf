#include "arm_dataproc_space_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../cpu/arm_processor_config.h"
#include "arm_opcode.h"
#include "cpu_state.h"
#include "decoded_insn.h"
#include "place_fns.h"

REGISTER_SERVICE(ArmDataprocSpaceDecoder);

bool ArmDataprocSpaceDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void ArmDataprocSpaceDecoder::OnReady() {
    processor_config_ = &emu_.Get<ArmProcessorConfig>();
}

/* DDI 0406C.c Table A5-2 (p. A5-196): op = insn[25], op1 = insn[24:20],
   op2 = insn[7:4]. */
bool ArmDataprocSpaceDecoder::Decode(DecodedInsn* insn, ArmOpcode op) {
    const uint32_t op1      = (op.word >> 20) & 0x1Fu;
    const uint32_t op2      = (op.word >>  4) & 0xFu;
    const bool     misc_row = (op1 & 0x19u) == 0x10u;

    if (((op.word >> 25) & 0x1u) == 0u) {
        if (op2 == 0x9u) {
            if ((op1 & 0x10u) == 0u) {
                /* DDI 0406C.c Table A5-7 (p. A5-202): op = insn[23:20];
                   0101 and 0111 are UNDEFINED. */
                const uint32_t mul = (op.word >> 20) & 0xFu;
                if (mul == 0x5u || mul == 0x7u) {
                    return false;
                }
                return MarkArmUnimplemented(insn, op.word);
            }
            return DecodeSyncSpace(insn, op);
        }
        if ((op2 & 0x9u) == 0x9u) {
            return DecodeExtraLoadStore(insn, op);
        }
        if (misc_row) {
            if ((op2 & 0x8u) == 0u) {
                return DecodeMiscSpace(insn, op);
            }
            /* DDI 0406C.c Table A5-9 (p. A5-203), A5.2.7: "available in
               ARMv5TE and above, and are UNDEFINED in earlier variants". */
            if (!processor_config_->HasDsp()) {
                return false;
            }
            return MarkArmUnimplemented(insn, op.word);
        }
        /* DDI 0406C.c Tables A5-3/A5-4 via Table A5-2 rows
           "Data-processing (register / register-shifted register)". */
        return MarkArmUnimplemented(insn, op.word);
    }

    if (!misc_row) {
        /* DDI 0406C.c Table A5-5 via Table A5-2 row
           "Data-processing (immediate)". */
        return MarkArmUnimplemented(insn, op.word);
    }
    if (op1 == 0x10u || op1 == 0x14u) {
        /* MOVW - DDI 0406C.c A8.8.102 encoding A2 (p. A8-484); MOVT -
           A8.8.106 encoding A1 (p. A8-491). Both: imm16 = imm4:imm12,
           d == 15 UNPREDICTABLE. */
        if (!processor_config_->HasMovwMovt()) {
            return false;
        }
        const uint32_t rd = (op.word >> 12) & 0xFu;
        if (rd == ArmGpr::kR15) {
            return false;
        }
        insn->rd        = rd;
        insn->immediate = ((op.word >> 4) & 0xF000u) | (op.word & 0xFFFu);
        insn->place_fn  = (op1 == 0x10u) ? &PlaceMovw : &PlaceMovt;
        return true;
    }
    return DecodeMsrImmHints(insn, op);
}

/* DDI 0406C.c Table A5-14 (p. A5-207): op2 = insn[6:4], B = insn[9],
   op = insn[22:21]; other encodings in this space are UNDEFINED. */
bool ArmDataprocSpaceDecoder::DecodeMiscSpace(DecodedInsn* insn, ArmOpcode op) {
    const uint32_t op2 = (op.word >>  4) & 0x7u;
    const uint32_t mop = (op.word >> 21) & 0x3u;

    switch (op2) {
    case 0u:
        /* B == 1 rows are the Banked-register MRS/MSR - v7VE only
           (DDI 0406C.c Table A5-14, p. A5-207). */
        if (((op.word >> 9) & 0x1u) != 0u) {
            return false;
        }
        insn->s = (op.word >> 21) & 0x1u;
        insn->n = (op.word >> 22) & 0x1u;
        if ((mop & 0x1u) == 0u) {
            /* MRS - DDI 0406C.c A8.8.109 encoding A1 (p. A8-496). */
            insn->rd = (op.word >> 12) & 0xFu;
        } else {
            /* MSR (register) - DDI 0406C.c A8.8.112 encoding A1
               (p. A8-500). */
            insn->rm  =  op.word        & 0xFu;
            insn->crn = (op.word >> 16) & 0xFu;
        }
        insn->place_fn = &PlaceMRSorMSR;
        return true;
    case 1u:
        if (mop == 1u) {
            /* BX - DDI 0406C.c A8.8.27 encoding A1 (p. A8-352). */
            if (!processor_config_->HasThumb()) {
                return false;
            }
            insn->rm           = op.word & 0xFu;
            insn->r15_modified = true;
            insn->place_fn     = &PlaceBx;
            return true;
        }
        if (mop == 3u) {
            /* CLZ - DDI 0406C.c A8.8.33 encoding A1 (p. A8-362):
               d == 15 || m == 15 UNPREDICTABLE. */
            if (!processor_config_->HasClz()) {
                return false;
            }
            const uint32_t rd = (op.word >> 12) & 0xFu;
            const uint32_t rm =  op.word        & 0xFu;
            if (rd == ArmGpr::kR15 || rm == ArmGpr::kR15) {
                return false;
            }
            insn->rd       = rd;
            insn->rm       = rm;
            insn->place_fn = &PlaceClz;
            return true;
        }
        return false;
    case 2u:
        if (mop == 1u) {
            /* BXJ - DDI 0406C.c A8.8.28 (p. A8-354). */
            return MarkArmUnimplemented(insn, op.word);
        }
        return false;
    case 3u:
        if (mop == 1u) {
            /* BLX (register) - DDI 0406C.c A8.8.26 encoding A1
               (p. A8-350): m == 15 UNPREDICTABLE. */
            if (!processor_config_->HasBlxReg()) {
                return false;
            }
            const uint32_t rm = op.word & 0xFu;
            if (rm == ArmGpr::kR15) {
                return false;
            }
            insn->rm           = rm;
            insn->r15_modified = true;
            insn->place_fn     = &PlaceBlxReg;
            return true;
        }
        return false;
    case 5u:
        /* DDI 0406C.c Table A5-8 (p. A5-202), A5.2.6: "available in
           ARMv5TE and above, and are UNDEFINED in earlier variants". */
        if (!processor_config_->HasDsp()) {
            return false;
        }
        return MarkArmUnimplemented(insn, op.word);
    case 7u:
        if (mop == 1u) {
            /* BKPT - DDI 0406C.c A8.8.24 (p. A8-346). */
            return MarkArmUnimplemented(insn, op.word);
        }
        if (mop == 3u && processor_config_->HasSecurityExtensions()) {
            /* SMC - DDI 0406C.c B9.3.14 (p. B9-2002). */
            return MarkArmUnimplemented(insn, op.word);
        }
        return false;
    default:
        return false;
    }
}

/* DDI 0406C.c Table A5-12 (p. A5-205): op = insn[23:20]; other encodings
   in this space are UNDEFINED. */
bool ArmDataprocSpaceDecoder::DecodeSyncSpace(DecodedInsn* insn, ArmOpcode op) {
    const uint32_t sop = (op.word >> 20) & 0xFu;
    if ((sop & 0x8u) != 0u) {
        return DecodeLdrexStrex(insn, op);
    }
    if ((sop & 0xBu) != 0u) {
        return false;
    }
    /* SWP / SWPB - DDI 0406C.c A8.8.229 encoding A1 (p. A8-722):
       B = insn[22], Rt2 = insn[3:0], insn[11:8] SBZ. */
    if (((op.word >> 8) & 0xFu) != 0u) {
        return false;
    }
    insn->rn       = (op.word >> 16) & 0xFu;
    insn->rd       = (op.word >> 12) & 0xFu;
    insn->rm       =  op.word        & 0xFu;
    insn->n        = (op.word >> 22) & 0x1u;
    insn->op1      = (op.word >>  5) & 0x3u;
    insn->place_fn = &PlaceLoadStoreExtension;
    return true;
}

bool ArmDataprocSpaceDecoder::DecodeLdrexStrex(DecodedInsn* insn, ArmOpcode op) {
    if (!processor_config_->HasLdrexStrex()) {
        return false;
    }

    const uint32_t bits27_23 = (op.word >> 23) & 0x1Fu;
    const uint32_t bits22_21 = (op.word >> 21) & 0x3u;
    const uint32_t l         = (op.word >> 20) & 0x1u;
    const uint32_t bits11_8  = (op.word >>  8) & 0xFu;
    const uint32_t bits7_4   = (op.word >>  4) & 0xFu;

    if (bits27_23 != 0b00011u || bits11_8 != 0xFu || bits7_4 != 0x9u) {
        return false;
    }
    if (bits22_21 != 0u) {
        /* DDI 0406C.c Table A5-12 (p. A5-205): the doubleword / byte /
           halfword rows (op 1010..1111) are v6K. */
        if (processor_config_->HasCp15V7()) {
            return MarkArmUnimplemented(insn, op.word);
        }
        return false;
    }

    const uint32_t rn = (op.word >> 16) & 0xFu;
    const uint32_t rd = (op.word >> 12) & 0xFu;
    const uint32_t rt =  op.word        & 0xFu;

    /* DDI 0100I A4.1.27 LDREX (p. A4-53), "Use of R15": "If register 15
       is specified for <Rd> or <Rn>, the result is UNPREDICTABLE." */
    if (rn == ArmGpr::kR15 || rd == ArmGpr::kR15) {
        return false;
    }

    if (l == 1u) {
        /* LDREX: bits[3:0] is SBO=0xF. */
        if (rt != 0xFu) {
            return false;
        }
        insn->rd       = rd;   /* Rt destination */
        insn->rn       = rn;   /* address */
        insn->place_fn = &PlaceLdrex;
        return true;
    }

    /* STREX: bits[3:0] is Rt (source). */
    if (rt == ArmGpr::kR15) {
        return false;
    }
    if (rd == rt || rd == rn) {
        return false;
    }
    insn->rd       = rd;       /* status output (0 success, 1 fail) */
    insn->rn       = rn;       /* address */
    insn->rm       = rt;       /* source value */
    insn->place_fn = &PlaceStrex;
    return true;
}

/* DDI 0406C.c Tables A5-10/A5-11 (pp. A5-203/A5-204): op2 = insn[6:5],
   I = insn[22], imm8 = imm4H:imm4L; the A5.2.9 unprivileged forms
   (P == 0 && W == 1) resolve inside EmitHalfwordSignedTransfer. */
bool ArmDataprocSpaceDecoder::DecodeExtraLoadStore(DecodedInsn* insn,
                                                   ArmOpcode    op) {
    insn->op1 = (op.word >>  5) & 0x3u;
    insn->n   = (op.word >> 22) & 0x1u;
    insn->l   = (op.word >> 20) & 0x1u;
    insn->w   = (op.word >> 21) & 0x1u;
    insn->u   = (op.word >> 23) & 0x1u;
    insn->p   = (op.word >> 24) & 0x1u;
    insn->rn  = (op.word >> 16) & 0xFu;
    insn->rd  = (op.word >> 12) & 0xFu;
    insn->rm  =  op.word        & 0xFu;
    const int32_t imm8 = static_cast<int32_t>(
        ((op.word >> 4) & 0xF0u) | (op.word & 0xFu));
    insn->offset   = insn->u ? imm8 : -imm8;
    insn->place_fn = &PlaceLoadStoreExtension;
    return true;
}

/* DDI 0406C.c Table A5-13 (p. A5-206): op = insn[22], op1 = insn[19:16],
   op2 = insn[7:0]; unallocated hints "behave as if op2 is set to
   0b00000000". */
bool ArmDataprocSpaceDecoder::DecodeMsrImmHints(DecodedInsn* insn,
                                                ArmOpcode    op) {
    const uint32_t r    = (op.word >> 22) & 0x1u;
    const uint32_t mask = (op.word >> 16) & 0xFu;

    if (r == 0u && mask == 0u && processor_config_->HasCp15V7()) {
        const uint32_t hint = op.word & 0xFFu;
        switch (hint) {
        case 0x00u:
            /* NOP - DDI 0406C.c Table A5-13 (p. A5-206). */
            insn->place_fn = &PlaceNop;
            return true;
        case 0x03u:
            /* WFI - DDI 0406C.c A8.8.425 (p. A8-1106). */
            insn->place_fn = &PlaceWfi;
            return true;
        case 0x01u:
        case 0x02u:
        case 0x04u:
            /* YIELD / WFE / SEV rows of Table A5-13 (p. A5-206). */
            return MarkArmUnimplemented(insn, op.word);
        default:
            if ((hint & 0xF0u) == 0xF0u) {
                /* DBG row of Table A5-13 (p. A5-206). */
                return MarkArmUnimplemented(insn, op.word);
            }
            insn->place_fn = &PlaceNop;
            return true;
        }
    }

    /* MSR (immediate) - DDI 0406C.c A8.8.111 encoding A1 (p. A8-498). */
    insn->crn       = mask;
    insn->n         = r;
    insn->immediate = op.word & 0xFFFu;
    insn->place_fn  = &PlaceMSRImmediate;
    return true;
}
