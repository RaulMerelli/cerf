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
    case 0x03u:
    case 0x04u:
    case 0x05u:
    case 0x06u:
    case 0x07u:
        return MarkArmUnimplemented(insn, op);
    case 0x08u:
        /* Figure A6-1 note 3, p. A6-5. */
        if (((op >> 10) & 0x1u) != 0u && ((op >> 8) & 0x3u) == 0x3u &&
            ((op >> 7) & 0x1u) != 0u && !processor_config_->HasBlxReg()) {
            return false;
        }
        return MarkArmUnimplemented(insn, op);
    case 0x09u:
    case 0x0Au:
    case 0x0Bu:
    case 0x0Cu:
    case 0x0Du:
    case 0x0Eu:
    case 0x0Fu:
    case 0x10u:
    case 0x11u:
    case 0x12u:
    case 0x13u:
    case 0x14u:
    case 0x15u:
        return MarkArmUnimplemented(insn, op);
    case 0x16u:
    case 0x17u:
        return DecodeMiscellaneous(insn, op);
    case 0x18u:
    case 0x19u:
    case 0x1Au:
        return MarkArmUnimplemented(insn, op);
    case 0x1Bu:
        switch ((op >> 8) & 0xFu) {
        case 0xEu:
            return false;
        default:
            return MarkArmUnimplemented(insn, op);
        }
    case 0x1Cu:
        return MarkArmUnimplemented(insn, op);
    case 0x1Du:
        /* Figure A6-1 note 4, p. A6-5. */
        if ((op & 0x1u) != 0u || !processor_config_->HasBlxReg()) return false;
        return MarkArmUnimplemented(insn, op);
    default:
        return MarkArmUnimplemented(insn, op);
    }
}
