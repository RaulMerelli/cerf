#include "thumb_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../cpu/arm_processor_config.h"
#include "decoded_insn.h"
#include "place_fns.h"
#include "thumb_transfer_decoder.h"

REGISTER_SERVICE(ThumbDecoder);

bool ThumbDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void ThumbDecoder::OnReady() {
    processor_config_ = &emu_.Get<ArmProcessorConfig>();
    transfer_decoder_ = &emu_.Get<ThumbTransferDecoder>();
}

/* ARM DDI 0100I A7.1.38 LSL (1) (p. A7-64), A7.1.40 LSR (1) (p. A7-68),
   A7.1.11 ASR (1) (p. A7-15). */
bool ThumbDecoder::DecodeShiftByImmediate(DecodedInsn* insn, uint16_t op) {
    const uint32_t shift_t = (op >> 11) & 0x3u;
    const uint32_t immed_5 = (op >>  6) & 0x1Fu;
    insn->op1      = 13u;
    insn->s        = 1u;
    /* DDI 0406C.c A8.8.103 MOV (register) T2 (p. A8-486): "setflags = TRUE"
       and "Not permitted in IT block", where A8.8.94 LSL (immediate)
       (p. A8-468) carries "setflags = !InITBlock()". */
    const bool mov_reg_t2 = shift_t == kSrLsl && immed_5 == 0u;
    insn->s_outside_it = mov_reg_t2 ? 0u : 1u;
    insn->und_in_it    = mov_reg_t2 ? 1u : 0u;
    insn->rn       = 0u;
    insn->rd       =  op       & 0x7u;
    insn->rm       = (op >> 3) & 0x7u;
    insn->n        = shift_t;
    insn->rs       =
        (shift_t != kSrLsl && immed_5 == 0u) ? 32u : immed_5;
    insn->place_fn = &PlaceDataProcessingReg;
    return true;
}

/* ARM DDI 0100I A7.1.5 (A7-7), A7.1.3 (A7-5), A7.1.67 (A7-115), A7.1.65
   (A7-113). */
bool ThumbDecoder::DecodeAddSubtract(DecodedInsn* insn, uint16_t op) {
    insn->op1 = ((op >> 9) & 0x1u) != 0u ? 2u : 4u;
    insn->s   = 1u;
    insn->s_outside_it = 1u;
    insn->rn  = (op >> 3) & 0x7u;
    insn->rd  =  op       & 0x7u;
    insn->rs  = 0u;
    if (((op >> 10) & 0x1u) != 0u) {
        insn->immediate = (op >> 6) & 0x7u;
        insn->place_fn  = &PlaceDataProcessing;
        return true;
    }
    insn->rm       = (op >> 6) & 0x7u;
    insn->n        = kSrLsl;
    insn->place_fn = &PlaceDataProcessingReg;
    return true;
}

/* ARM DDI 0100I A7.1.42 (A7-72), A7.1.21 (A7-35), A7.1.4 (A7-6), A7.1.66
   (A7-114). */
bool ThumbDecoder::DecodeImmediateOperations(DecodedInsn* insn, uint16_t op) {
    const uint32_t reg = (op >> 8) & 0x7u;
    insn->s         = 1u;
    insn->s_outside_it = 1u;
    insn->rs        = 0u;
    insn->immediate = op & 0xFFu;
    switch ((op >> 11) & 0x3u) {
    case 0u:
        insn->op1 = 13u;
        insn->rd  = reg;
        break;
    case 1u:
        /* DDI 0406C.c A8.8.37 CMP (immediate) (p. A8-370): "It updates the
           condition flags based on the result, and discards the result" - T1
           carries no setflags assignment. */
        insn->op1 = 10u;
        insn->rn  = reg;
        insn->s_outside_it = 0u;
        break;
    case 2u:
        insn->op1 = 4u;
        insn->rd  = reg;
        insn->rn  = reg;
        break;
    default:
        insn->op1 = 2u;
        insn->rd  = reg;
        insn->rn  = reg;
        break;
    }
    insn->place_fn = &PlaceDataProcessing;
    return true;
}

/* ARM DDI 0100I A7.1.6 ADD (4) (p. A7-8), A7.1.23 CMP (3) (p. A7-37),
   A7.1.44 MOV (3) (p. A7-75), A7.1.25 CPY (p. A7-41). */
bool ThumbDecoder::DecodeSpecialDataProcessing(DecodedInsn* insn,
                                               uint16_t     op) {
    const uint32_t h1  = (op >> 7) & 0x1u;
    const uint32_t h2  = (op >> 6) & 0x1u;
    const bool     low = h1 == 0u && h2 == 0u;
    const uint32_t reg = (h1 << 3) | (op & 0x7u);
    insn->rm = (h2 << 3) | ((op >> 3) & 0x7u);
    insn->n  = kSrLsl;
    insn->rs = 0u;
    switch ((op >> 8) & 0x3u) {
    case 0u:
        /* ARM DDI 0406C.c Table A6-4 (A6.2.3, p. A6-226): opcode 0000 is Add Low
           Registers, variant v6T2, footnote a "UNPREDICTABLE in earlier
           variants". */
        if (low) {
            if (!processor_config_->HasThumb2()) return false;
            return MarkArmUnimplemented(insn, op);
        }
        insn->op1 = 4u;
        insn->s   = 0u;
        insn->rn  = reg;
        insn->rd  = reg;
        break;
    case 1u:
        if (low || reg == 15u) return false;
        insn->op1 = 10u;
        insn->s   = 1u;
        insn->rn  = reg;
        break;
    case 2u:
        if (low && !processor_config_->HasCp15V6()) return false;
        insn->op1 = 13u;
        insn->s   = 0u;
        insn->rd  = reg;
        break;
    default:
        return MarkArmUnimplemented(insn, op);
    }
    insn->place_fn = &PlaceDataProcessingReg;
    return true;
}

/* ARM DDI 0100I A7.1.19 BX (p. A7-32), A7.1.18 BLX (2) (p. A7-30). */
bool ThumbDecoder::DecodeBranchExchange(DecodedInsn* insn, uint16_t op) {
    const bool link = ((op >> 7) & 0x1u) != 0u;
    insn->rm = (((op >> 6) & 0x1u) << 3) | ((op >> 3) & 0x7u);
    if (link && insn->rm == 15u) return false;
    insn->place_fn = link ? &PlaceBlxReg : &PlaceBx;
    return true;
}

/* ARM DDI 0100I A7.1.39 LSL (2) (p. A7-66), A7.1.41 LSR (2) (p. A7-70),
   A7.1.12 ASR (2) (p. A7-17), A7.1.54 ROR (p. A7-92). */
bool ThumbDecoder::DecodeShiftByRegister(DecodedInsn* insn, uint16_t op,
                                         uint32_t type) {
    insn->op1      = 13u;
    insn->s        = 1u;
    insn->s_outside_it = 1u;
    insn->n        = type;
    insn->rd       =  op       & 0x7u;
    insn->rm       =  op       & 0x7u;
    insn->rs       = (op >> 3) & 0x7u;
    insn->place_fn = &PlaceDataProcessingShiftedReg;
    return true;
}

/* ARM DDI 0100I A7.1.10 AND (p. A7-14), A7.1.26 EOR (p. A7-43), A7.1.2 ADC
   (p. A7-4), A7.1.55 SBC (p. A7-94), A7.1.72 TST (p. A7-122), A7.1.22 CMP (2)
   (p. A7-36), A7.1.20 CMN (p. A7-34), A7.1.48 ORR (p. A7-81), A7.1.15 BIC
   (p. A7-23), A7.1.46 MVN (p. A7-79). */
bool ThumbDecoder::DecodeAluOperations(DecodedInsn* insn, uint16_t op) {
    const uint32_t opcode = (op >> 6) & 0xFu;
    const uint32_t reg    =  op       & 0x7u;
    switch (opcode) {
    case 0x0u:
    case 0x1u:
    case 0x5u:
    case 0x6u:
    case 0xCu:
    case 0xEu:
        insn->rn = reg;
        insn->rd = reg;
        insn->s_outside_it = 1u;
        break;
    case 0x8u:
    case 0xAu:
    case 0xBu:
        insn->rn = reg;
        break;
    case 0xFu:
        insn->rd = reg;
        insn->s_outside_it = 1u;
        break;
    case 0x9u:
        /* ARM DDI 0100I A7.1.47 NEG, p. A7-80. */
        insn->op1       = 3u;
        insn->s         = 1u;
        insn->s_outside_it = 1u;
        insn->rn        = (op >> 3) & 0x7u;
        insn->rd        = reg;
        insn->immediate = 0u;
        insn->rs        = 0u;
        insn->place_fn  = &PlaceDataProcessing;
        return true;
    case 0xDu: {
        /* ARM DDI 0100I A7.1.45 MUL, p. A7-77. */
        const uint32_t rm = (op >> 3) & 0x7u;
        if (rm == reg && !processor_config_->HasCp15V6()) return false;
        insn->op1      = 0u;
        insn->s        = 1u;
        insn->s_outside_it = 1u;
        insn->rd       = reg;
        insn->rm       = reg;
        insn->rn       = rm;
        insn->place_fn = &PlaceMultiply;
        return true;
    }
    case 0x2u:
        return DecodeShiftByRegister(insn, op, kSrLsl);
    case 0x3u:
        return DecodeShiftByRegister(insn, op, kSrLsr);
    case 0x4u:
        return DecodeShiftByRegister(insn, op, kSrAsr);
    case 0x7u:
        return DecodeShiftByRegister(insn, op, kSrRor);
    default:
        return MarkArmUnimplemented(insn, op);
    }
    insn->op1      = opcode;
    insn->s        = 1u;
    insn->rm       = (op >> 3) & 0x7u;
    insn->n        = kSrLsl;
    insn->rs       = 0u;
    insn->place_fn = &PlaceDataProcessingReg;
    return true;
}

/* ARM DDI 0100I A7.1.7 ADD (5) (p. A7-10), A7.1.8 ADD (6) (p. A7-11). */
bool ThumbDecoder::DecodeAddToPcOrSp(DecodedInsn* insn, uint16_t op) {
    const uint32_t imm = (op & 0xFFu) * 4u;
    const bool     sp  = ((op >> 11) & 0x1u) != 0u;
    insn->op1       = 4u;
    insn->s         = 0u;
    insn->rs        = 0u;
    insn->rd        = (op >> 8) & 0x7u;
    insn->rn        = sp ? 13u : 15u;
    insn->immediate = sp ? imm : imm - ((insn->guest_address + 4u) & 3u);
    insn->place_fn  = &PlaceDataProcessing;
    return true;
}

/* ARM DDI 0100I A7.1.13 B (1), p. A7-19. */
bool ThumbDecoder::DecodeConditionalBranch(DecodedInsn* insn, uint16_t op) {
    const uint32_t imm8 = op & 0xFFu;
    insn->l        = 0u;
    insn->offset   = static_cast<int32_t>(((imm8 ^ 0x80u) - 0x80u) << 1);
    insn->place_fn = &PlaceBranch;
    return true;
}

/* ARM DDI 0100I A7.1.14 B (2), p. A7-21. */
bool ThumbDecoder::DecodeUnconditionalBranch(DecodedInsn* insn, uint16_t op) {
    const uint32_t off11 = op & 0x7FFu;
    insn->l        = 0u;
    insn->offset   = static_cast<int32_t>(((off11 ^ 0x400u) - 0x400u) << 1);
    insn->place_fn = &PlaceBranch;
    return true;
}

/* ARM DDI 0100I A7.1.17 BL, BLX (1), H == 10 (p. A7-27). */
bool ThumbDecoder::DecodeBranchLinkPrefix(DecodedInsn* insn, uint16_t op) {
    const uint32_t off11 = op & 0x7FFu;
    insn->offset   = static_cast<int32_t>(((off11 ^ 0x400u) - 0x400u) << 12);
    insn->place_fn = &PlaceThumbBlPrefix;
    return true;
}

/* ARM DDI 0100I A7.1.17 BL, BLX (1), H == 11 (p. A7-27). */
bool ThumbDecoder::DecodeBranchLinkSuffix(DecodedInsn* insn, uint16_t op) {
    insn->offset   = static_cast<int32_t>((op & 0x7FFu) << 1);
    insn->place_fn = &PlaceThumbBlSuffix;
    return true;
}

/* ARM DDI 0100I A7.1.9 ADD (7) (p. A7-12), A7.1.68 SUB (4) (p. A7-116). */
bool ThumbDecoder::DecodeAdjustStackPointer(DecodedInsn* insn, uint16_t op) {
    insn->op1       = ((op >> 7) & 0x1u) != 0u ? 2u : 4u;
    insn->s         = 0u;
    insn->rs        = 0u;
    insn->rn        = 13u;
    insn->rd        = 13u;
    insn->immediate = (op & 0x7Fu) * 4u;
    insn->place_fn  = &PlaceDataProcessing;
    return true;
}

/* ARM DDI 0406C.c A8.8.54 IT encoding T1 (p. A8-390): firstcond = bits[7:4],
   mask = bits[3:0]; "if mask == '0000' then SEE Related encodings"; "if
   firstcond == '1111' || (firstcond == '1110' && BitCount(mask) != 1) then
   UNPREDICTABLE". */
bool ThumbDecoder::DecodeIfThen(DecodedInsn* insn, uint16_t op) {
    const uint32_t firstcond = (op >> 4) & 0xFu;
    const uint32_t mask      =  op       & 0xFu;
    if (mask == 0u) {
        return MarkArmUnimplemented(insn, op);
    }
    uint32_t set_bits = 0u;
    for (uint32_t b = 0u; b < 4u; ++b) {
        set_bits += (mask >> b) & 0x1u;
    }
    if (firstcond == 0xFu || (firstcond == 0xEu && set_bits != 1u)) {
        return false;
    }
    insn->itstate       = (firstcond << 4) | mask;
    insn->itstate_valid = 1u;
    insn->place_fn      = &PlaceNop;
    return true;
}

/* ARM DDI 0100I Figure A6-2 (A6.2.1, p. A6-5), bits[15:12] == 0b1011, and its
   closing note: "Any instruction with bits[15:12] = 1011, and which is not
   shown in Figure A6-2, is an Undefined instruction." */
bool ThumbDecoder::DecodeMiscellaneous(DecodedInsn* insn, uint16_t op) {
    switch ((op >> 8) & 0xFu) {
    case 0x0u:
        return DecodeAdjustStackPointer(insn, op);
    case 0x2u:
        /* Figure A6-2 note 2, p. A6-5. */
        if (!processor_config_->HasExtendRotate()) return false;
        return MarkArmUnimplemented(insn, op);
    case 0x4u:
    case 0x5u:
    case 0xCu:
    case 0xDu: {
        /* A7.1.50 PUSH, p. A7-85 (SP decrement-before, R adds LR); A7.1.49
           POP, p. A7-82 (SP increment-after, R adds PC). Both are
           UNPREDICTABLE when bits[8:0] are zero. */
        if ((op & 0x1FFu) == 0u) return false;
        const bool     load = ((op >> 11) & 0x1u) != 0u;
        const uint32_t r    = (op >> 8) & 0x1u;
        insn->register_list = static_cast<uint16_t>(
            (op & 0xFFu) | (r << (load ? 15u : 14u)));
        insn->rn           = 13u;
        insn->l            = load ? 1u : 0u;
        insn->p            = load ? 0u : 1u;
        insn->u            = load ? 1u : 0u;
        insn->w            = 1u;
        insn->s            = 0u;
        insn->r15_modified = load && r != 0u;
        insn->place_fn     = &PlaceBlockDataTransfer;
        return true;
    }
    case 0x6u: {
        const uint32_t lo = op & 0xFFu;
        /* Set Endianness: bits[7:4] == 0b0101. Change Processor State:
           bits[7:5] == 0b011 with bit[3] == 0. Figure A6-2 note 2, p. A6-5. */
        const bool setend = (lo & 0xF0u) == 0x50u;
        const bool cps    = (lo & 0xE0u) == 0x60u && (lo & 0x08u) == 0u;
        if (!setend && !cps) return false;
        if (!processor_config_->HasCp15V6()) return false;
        return MarkArmUnimplemented(insn, op);
    }
    case 0x1u:
    case 0x3u:
    case 0x9u:
    case 0xBu:
        /* ARM DDI 0406C.c Table A6-6 (A6.2.5, p. A6-228) allocates opcode
           0001xxx/0011xxx/1001xxx/1011xxx to CBNZ, CBZ (variant v6T2) and
           1111xxx to If-Then, and hints; "Other encodings in this space are
           UNDEFINED." */
        if (!processor_config_->HasThumb2()) return false;
        return MarkArmUnimplemented(insn, op);
    case 0xFu:
        if (!processor_config_->HasThumb2()) return false;
        return DecodeIfThen(insn, op);
    case 0xAu:
        /* Figure A6-2 note 2, p. A6-5. */
        if (!processor_config_->HasRev()) return false;
        return MarkArmUnimplemented(insn, op);
    case 0xEu:
        /* Figure A6-2 note 1, p. A6-5. DDI 0406C.c A8.8.24 BKPT encoding T1
           (p. A8-346): "Breakpoint is always unconditional, even when inside
           an IT block." */
        if (!processor_config_->HasBlxReg()) return false;
        insn->uncond_in_it = 1u;
        return MarkArmUnimplemented(insn, op);
    default:
        return false;
    }
}

/* ARM DDI 0100I Figure A6-1 (A6.2, p. A6-4): bits[15:11] select the Thumb
   instruction class. */
bool ThumbDecoder::DecodeThumb(DecodedInsn* insn, uint16_t op) {
    const uint32_t row = (op >> 11) & 0x1Fu;
    /* Figure A6-1, p. A6-4: the conditional branch row is the only encoding
       carrying a cond field. Note 2, p. A6-5: "The cond field is not allowed
       to be 1110 or 1111 in this line." */
    insn->cond = 0xEu;
    if (row == 0x1Au || row == 0x1Bu) {
        const uint32_t cond = (op >> 8) & 0xFu;
        if (cond < 0xEu) insn->cond = cond;
    }
    /* Figure A6-1, p. A6-4: branch/exchange, conditional branch, software
       interrupt, unconditional branch, BLX suffix and BL suffix write the PC. */
    /* Figure A6-1, p. A6-4: on the 010001 line opcode 11 is branch/exchange.
       A7.1.6 ADD (4), p. A7-8 and A7.1.44 MOV (3), p. A7-75: Rd is H1 then
       Rd[2:0], "any of R0 to R15". A7.1.23 CMP (3), p. A7-37: that field is
       Rn. */
    const bool     high_line = row == 0x08u && ((op >> 10) & 0x1u) != 0u;
    const uint32_t opcode98  = (op >> 8) & 0x3u;
    const uint32_t high_rd   = (((op >> 7) & 0x1u) << 3) | (op & 0x7u);
    insn->r15_modified =
        (high_line && opcode98 == 0x3u) ||
        (high_line && (opcode98 == 0x0u || opcode98 == 0x2u) &&
         high_rd == 15u) ||
        row == 0x1Au || row == 0x1Bu || row == 0x1Cu || row == 0x1Du ||
        row == 0x1Fu;

    switch (row) {
    case 0x00u:
    case 0x01u:
    case 0x02u:
        return DecodeShiftByImmediate(insn, op);
    case 0x03u:
        return DecodeAddSubtract(insn, op);
    case 0x04u:
    case 0x05u:
    case 0x06u:
    case 0x07u:
        return DecodeImmediateOperations(insn, op);
    case 0x08u:
        /* Figure A6-1 note 3, p. A6-5. */
        if (((op >> 10) & 0x1u) != 0u && ((op >> 8) & 0x3u) == 0x3u &&
            ((op >> 7) & 0x1u) != 0u && !processor_config_->HasBlxReg()) {
            return false;
        }
        if (((op >> 10) & 0x1u) != 0u && ((op >> 8) & 0x3u) != 0x3u) {
            return DecodeSpecialDataProcessing(insn, op);
        }
        if (((op >> 10) & 0x1u) != 0u && ((op >> 8) & 0x3u) == 0x3u) {
            return DecodeBranchExchange(insn, op);
        }
        return DecodeAluOperations(insn, op);
    case 0x09u:
        return transfer_decoder_->DecodeLoadLiteral(insn, op);
    case 0x0Au:
    case 0x0Bu:
        return transfer_decoder_->DecodeRegisterOffsetTransfer(insn, op);
    case 0x0Cu:
    case 0x0Du:
    case 0x0Eu:
    case 0x0Fu:
        return transfer_decoder_->DecodeImmediateOffsetTransfer(insn, op);
    case 0x10u:
    case 0x11u:
        return transfer_decoder_->DecodeHalfwordOffsetTransfer(insn, op);
    case 0x12u:
    case 0x13u:
        return transfer_decoder_->DecodeStackRelativeTransfer(insn, op);
    case 0x14u:
    case 0x15u:
        return DecodeAddToPcOrSp(insn, op);
    case 0x16u:
    case 0x17u:
        return DecodeMiscellaneous(insn, op);
    case 0x18u:
    case 0x19u:
        return transfer_decoder_->DecodeLoadStoreMultiple(insn, op);
    case 0x1Au:
        return DecodeConditionalBranch(insn, op);
    case 0x1Bu:
        switch ((op >> 8) & 0xFu) {
        case 0xEu:
            return false;
        case 0xFu:
            return MarkArmUnimplemented(insn, op);
        default:
            return DecodeConditionalBranch(insn, op);
        }
    case 0x1Cu:
        return DecodeUnconditionalBranch(insn, op);
    case 0x1Du:
        /* Figure A6-1 note 4, p. A6-5. */
        if ((op & 0x1u) != 0u || !processor_config_->HasBlxReg()) return false;
        return MarkArmUnimplemented(insn, op);
    case 0x1Eu:
        return DecodeBranchLinkPrefix(insn, op);
    case 0x1Fu:
        return DecodeBranchLinkSuffix(insn, op);
    default:
        return MarkArmUnimplemented(insn, op);
    }
}
