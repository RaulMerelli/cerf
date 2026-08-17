#include "arm_dataproc_space_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../cpu/arm_processor_config.h"
#include "arm_multiply_space_decoder.h"
#include "arm_opcode.h"
#include "cpu_state.h"
#include "decoded_insn.h"
#include "place_fns.h"

REGISTER_SERVICE(ArmDataprocSpaceDecoder);

bool ArmDataprocSpaceDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void ArmDataprocSpaceDecoder::OnReady() {
    multiply_decoder_ = &emu_.Get<ArmMultiplySpaceDecoder>();
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
                return multiply_decoder_->Decode(insn, op);
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
            return multiply_decoder_->DecodeHalfword(insn, op);
        }
        /* DDI 0406C.c Table A5-2 (p. A5-196): op2 xxx0 selects
           Data-processing (register), op2 0xx1 register-shifted
           register. */
        if ((op2 & 0x1u) == 0u) {
            return DecodeDataProcessingReg(insn, op);
        }
        return DecodeDataProcessingShiftedReg(insn, op);
    }

    if (!misc_row) {
        return DecodeDataProcessingImm(insn, op);
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

/* DDI 0406C.c Table A5-5 (p. A5-199): op = insn[24:20]; A5.2.4
   (pp. A5-200/201): imm32 = imm8 ROR (2 * imm12<11:8>). MOV / MVN A1
   (pp. A8-484 / A8-504): Rn is (0); TST A1 (p. A8-744, repeated for
   TEQ / CMP / CMN): Rd is (0). */
bool ArmDataprocSpaceDecoder::DecodeDataProcessingImm(DecodedInsn* insn,
                                                      ArmOpcode    op) {
    const uint32_t opcode = (op.word >> 21) & 0xFu;
    const uint32_t s      = (op.word >> 20) & 0x1u;
    const uint32_t rn     = (op.word >> 16) & 0xFu;
    const uint32_t rd     = (op.word >> 12) & 0xFu;
    const uint32_t rot2   = ((op.word >> 8) & 0xFu) * 2u;
    const uint32_t imm8   =  op.word        & 0xFFu;
    const uint32_t imm32  =
        rot2 ? ((imm8 >> rot2) | (imm8 << (32u - rot2))) : imm8;

    const bool is_test = opcode >= 8u && opcode <= 11u;
    const bool is_move = opcode == 13u || opcode == 15u;
    if (is_move && rn != 0u) {
        return false;
    }
    if (is_test && rd != 0u) {
        return false;
    }

    insn->op1       = opcode;
    insn->s         = s;
    insn->rn        = rn;
    insn->rd        = rd;
    insn->rs        = rot2;
    insn->immediate = imm32;
    if (rd == ArmGpr::kR15 && !is_test) {
        insn->r15_modified = true;
        if (s != 0u) {
            insn->is_exception_return = true;
        }
    }
    insn->place_fn = &PlaceDataProcessing;
    return true;
}

/* DDI 0406C.c Table A5-3 (p. A5-197): op = insn[24:20], op2 = insn[6:5],
   imm5 = insn[11:7]; (shift_t, shift_n) = DecodeImmShift(type, imm5) per
   A8.4.3 (pp. A8-292/293). MOV / LSL / LSR / ASR / RRX / ROR / MVN A1
   (pp. A8-488/468/472/330/572/568/506): Rn is (0); TST A1 (p. A8-746,
   repeated for TEQ / CMP / CMN): Rd is (0). */
bool ArmDataprocSpaceDecoder::DecodeDataProcessingReg(DecodedInsn* insn,
                                                      ArmOpcode    op) {
    const uint32_t opcode = (op.word >> 21) & 0xFu;
    const uint32_t s      = (op.word >> 20) & 0x1u;
    const uint32_t rn     = (op.word >> 16) & 0xFu;
    const uint32_t rd     = (op.word >> 12) & 0xFu;
    const uint32_t imm5   = (op.word >>  7) & 0x1Fu;
    const uint32_t type   = (op.word >>  5) & 0x3u;
    const uint32_t rm     =  op.word        & 0xFu;

    const bool is_test = opcode >= 8u && opcode <= 11u;
    const bool is_move = opcode == 13u || opcode == 15u;
    if (is_move && rn != 0u) {
        return false;
    }
    if (is_test && rd != 0u) {
        return false;
    }

    uint32_t shift_t = type;
    uint32_t shift_n = imm5;
    if ((type == 1u || type == 2u) && imm5 == 0u) {
        shift_n = 32u;
    } else if (type == 3u && imm5 == 0u) {
        shift_t = kSrRrx;
        shift_n = 1u;
    }

    insn->op1 = opcode;
    insn->s   = s;
    insn->rn  = rn;
    insn->rd  = rd;
    insn->rm  = rm;
    insn->n   = shift_t;
    insn->rs  = shift_n;
    if (rd == ArmGpr::kR15 && !is_test) {
        insn->r15_modified = true;
        if (s != 0u) {
            insn->is_exception_return = true;
        }
    }
    insn->place_fn = &PlaceDataProcessingReg;
    return true;
}

/* DDI 0406C.c Table A5-4 (p. A5-198): op = insn[24:20], type = insn[6:5],
   Rs = insn[11:8]; shift_t = DecodeRegShift(type), shift amount =
   UInt(R[s]<7:0>) (A8.4.3 pp. A8-292/293). Every A1 encoding is
   UNPREDICTABLE when any named register is 15 (A8.8.15 p. A8-328,
   A8.8.3 p. A8-304, A8.8.242 p. A8-748, A8.8.117 p. A8-508, A8.8.95
   p. A8-470); TST/TEQ/CMP/CMN A1: Rd is (0); MVN and the shift aliases
   A1: Rn is (0). */
bool ArmDataprocSpaceDecoder::DecodeDataProcessingShiftedReg(
        DecodedInsn* insn, ArmOpcode op) {
    const uint32_t opcode = (op.word >> 21) & 0xFu;
    const uint32_t s      = (op.word >> 20) & 0x1u;
    const uint32_t rn     = (op.word >> 16) & 0xFu;
    const uint32_t rd     = (op.word >> 12) & 0xFu;
    const uint32_t rs     = (op.word >>  8) & 0xFu;
    const uint32_t type   = (op.word >>  5) & 0x3u;
    const uint32_t rm     =  op.word        & 0xFu;

    const bool is_test = opcode >= 8u && opcode <= 11u;
    const bool is_move = opcode == 13u || opcode == 15u;
    if (is_move && rn != 0u) {
        return false;
    }
    if (is_test && rd != 0u) {
        return false;
    }
    if (rm == ArmGpr::kR15 || rs == ArmGpr::kR15) {
        return false;
    }
    if (!is_test && rd == ArmGpr::kR15) {
        return false;
    }
    if (!is_move && rn == ArmGpr::kR15) {
        return false;
    }

    insn->op1      = opcode;
    insn->s        = s;
    insn->rn       = rn;
    insn->rd       = rd;
    insn->rm       = rm;
    insn->rs       = rs;
    insn->n        = type;
    insn->place_fn = &PlaceDataProcessingShiftedReg;
    return true;
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
    case 5u: {
        /* DDI 0406C.c Table A5-8 (p. A5-202), A5.2.6: "available in
           ARMv5TE and above, and are UNDEFINED in earlier variants".
           A1 encodings (QADD p. A8-540, QSUB A8-554, QDADD A8-548,
           QDSUB A8-550): Rn = insn[19:16], Rd = insn[15:12],
           insn[11:8] is (0), Rm = insn[3:0]; d / n / m == 15
           UNPREDICTABLE. */
        if (!processor_config_->HasDsp()) {
            return false;
        }
        if (((op.word >> 8) & 0xFu) != 0u) {
            return false;
        }
        const uint32_t rn = (op.word >> 16) & 0xFu;
        const uint32_t rd = (op.word >> 12) & 0xFu;
        const uint32_t rm =  op.word        & 0xFu;
        if (rn == ArmGpr::kR15 || rd == ArmGpr::kR15 || rm == ArmGpr::kR15) {
            return false;
        }
        insn->op1      = mop;
        insn->rn       = rn;
        insn->rd       = rd;
        insn->rm       = rm;
        insn->place_fn = &PlaceSaturatingArith;
        return true;
    }
    case 7u:
        if (mop == 1u) {
            /* BKPT - DDI 0406C.c A8.8.24 (p. A8-346). */
            insn->r15_modified = true;
            insn->place_fn = &PlaceBkpt;
            return true;
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
