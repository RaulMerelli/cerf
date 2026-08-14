#include "thumb_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../cpu/arm_processor_config.h"
#include "decoded_insn.h"
#include "place_fns.h"

REGISTER_SERVICE(ThumbDecoder);

bool ThumbDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void ThumbDecoder::OnReady() {
    processor_config_ = &emu_.Get<ArmProcessorConfig>();
}

/* ARM DDI 0100I A7.1.5 (A7-7), A7.1.3 (A7-5), A7.1.67 (A7-115), A7.1.65
   (A7-113). */
bool ThumbDecoder::DecodeAddSubtract(DecodedInsn* insn, uint16_t op) {
    insn->op1 = ((op >> 9) & 0x1u) != 0u ? 2u : 4u;
    insn->s   = 1u;
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
    insn->rs        = 0u;
    insn->immediate = op & 0xFFu;
    switch ((op >> 11) & 0x3u) {
    case 0u:
        insn->op1 = 13u;
        insn->rd  = reg;
        break;
    case 1u:
        insn->op1 = 10u;
        insn->rn  = reg;
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

/* ARM DDI 0100I A7.1.30 LDR (3), p. A7-51. */
bool ThumbDecoder::DecodeLoadLiteral(DecodedInsn* insn, uint16_t op) {
    const uint32_t pc = insn->guest_address + 4u;
    insn->p        = 1u;
    insn->u        = 1u;
    insn->s        = 0u;
    insn->w        = 0u;
    insn->l        = 1u;
    insn->n        = 1u;
    insn->rn       = 15u;
    insn->rd       = (op >> 8) & 0x7u;
    insn->offset   = static_cast<int32_t>((op & 0xFFu) * 4u) -
                     static_cast<int32_t>(pc & 3u);
    insn->place_fn = &PlaceSingleDataTransfer;
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
        if (low) return false;
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

/* ARM DDI 0100I A7.1.59 STR (2) (p. A7-101), A7.1.64 STRH (2) (p. A7-111),
   A7.1.62 STRB (2) (p. A7-107), A7.1.36 LDRSB (p. A7-61), A7.1.29 LDR (2)
   (p. A7-49), A7.1.35 LDRH (2) (p. A7-59), A7.1.33 LDRB (2) (p. A7-56),
   A7.1.37 LDRSH (p. A7-62). */
bool ThumbDecoder::DecodeRegisterOffsetTransfer(DecodedInsn* insn,
                                                uint16_t     op) {
    insn->p  = 1u;
    insn->u  = 1u;
    insn->w  = 0u;
    insn->n  = 0u;
    insn->rm = (op >> 6) & 0x7u;
    insn->rn = (op >> 3) & 0x7u;
    insn->rd =  op       & 0x7u;
    switch ((op >> 9) & 0x7u) {
    case 0u:
        insn->s = 0u;
        insn->l = 0u;
        break;
    case 2u:
        insn->s = 1u;
        insn->l = 0u;
        break;
    case 4u:
        insn->s = 0u;
        insn->l = 1u;
        break;
    case 6u:
        insn->s = 1u;
        insn->l = 1u;
        break;
    case 1u:
        insn->op1      = 1u;
        insn->l        = 0u;
        insn->place_fn = &PlaceLoadStoreExtension;
        return true;
    case 5u:
        insn->op1      = 1u;
        insn->l        = 1u;
        insn->place_fn = &PlaceLoadStoreExtension;
        return true;
    case 3u:
        insn->op1      = 2u;
        insn->l        = 1u;
        insn->place_fn = &PlaceLoadStoreExtension;
        return true;
    default:
        insn->op1      = 3u;
        insn->l        = 1u;
        insn->place_fn = &PlaceLoadStoreExtension;
        return true;
    }
    insn->op1      = kSrLsl;
    insn->rs       = 0u;
    insn->place_fn = &PlaceSingleDataTransfer;
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

/* ARM DDI 0100I A7.1.60 STR (3) (p. A7-104), A7.1.31 LDR (4) (p. A7-54). */
bool ThumbDecoder::DecodeStackRelativeTransfer(DecodedInsn* insn, uint16_t op) {
    insn->p        = 1u;
    insn->u        = 1u;
    insn->s        = 0u;
    insn->w        = 0u;
    insn->l        = (op >> 11) & 0x1u;
    insn->n        = 1u;
    insn->rn       = 13u;
    insn->rd       = (op >> 8) & 0x7u;
    insn->offset   = static_cast<int32_t>((op & 0xFFu) * 4u);
    insn->place_fn = &PlaceSingleDataTransfer;
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

/* ARM DDI 0100I Figure A6-2 (A6.2.1, p. A6-5), bits[15:12] == 0b1011, and its
   closing note: "Any instruction with bits[15:12] = 1011, and which is not
   shown in Figure A6-2, is an Undefined instruction." */
bool ThumbDecoder::DecodeMiscellaneous(DecodedInsn* insn, uint16_t op) {
    switch ((op >> 8) & 0xFu) {
    case 0x0u:
        return MarkArmUnimplemented(insn, op);
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
    case 0xAu:
        /* Figure A6-2 note 2, p. A6-5. */
        if (!processor_config_->HasRev()) return false;
        return MarkArmUnimplemented(insn, op);
    case 0xEu:
        /* Figure A6-2 note 1, p. A6-5. */
        if (!processor_config_->HasBlxReg()) return false;
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
        return MarkArmUnimplemented(insn, op);
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
        return MarkArmUnimplemented(insn, op);
    case 0x09u:
        return DecodeLoadLiteral(insn, op);
    case 0x0Au:
    case 0x0Bu:
        return DecodeRegisterOffsetTransfer(insn, op);
    case 0x0Cu:
    case 0x0Du:
    case 0x0Eu:
    case 0x0Fu:
    case 0x10u:
    case 0x11u:
        return MarkArmUnimplemented(insn, op);
    case 0x12u:
    case 0x13u:
        return DecodeStackRelativeTransfer(insn, op);
    case 0x14u:
    case 0x15u:
        return DecodeAddToPcOrSp(insn, op);
    case 0x16u:
    case 0x17u:
        return DecodeMiscellaneous(insn, op);
    case 0x18u:
    case 0x19u:
        return MarkArmUnimplemented(insn, op);
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
        return MarkArmUnimplemented(insn, op);
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
