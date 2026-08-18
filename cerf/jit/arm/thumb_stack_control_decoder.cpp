#include "thumb_stack_control_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../cpu/arm_processor_config.h"
#include "decoded_insn.h"
#include "place_fns.h"

REGISTER_SERVICE(ThumbStackControlDecoder);

bool ThumbStackControlDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void ThumbStackControlDecoder::OnReady() {
    processor_config_ = &emu_.Get<ArmProcessorConfig>();
}

/* ARM DDI 0100I A7.1.9 ADD (7) (p. A7-12), A7.1.68 SUB (4) (p. A7-116). */
bool ThumbStackControlDecoder::DecodeAdjustStackPointer(DecodedInsn* insn,
                                                        uint16_t op) {
    insn->op1       = ((op >> 7) & 0x1u) != 0u ? 2u : 4u;
    insn->s         = 0u;
    insn->rs        = 0u;
    insn->rn        = 13u;
    insn->rd        = 13u;
    insn->immediate = (op & 0x7Fu) * 4u;
    insn->place_fn  = &PlaceDataProcessing;
    return true;
}

/* ARM DDI 0406C.c A8.8.29 CBNZ, CBZ encoding T1 (p. A8-356): op = bit[11],
   i = bit[9], imm5 = bits[7:3], Rn = bits[2:0]; "n = UInt(Rn); imm32 =
   ZeroExtend(i:imm5:'0', 32); nonzero = (op == '1'); if InITBlock() then
   UNPREDICTABLE". */
bool ThumbStackControlDecoder::DecodeCompareAndBranch(DecodedInsn* insn,
                                                      uint16_t op) {
    insn->op1             = (op >> 11) & 0x1u;
    insn->rn              =  op        & 0x7u;
    insn->l               = 0u;
    insn->offset          = static_cast<int32_t>((((op >> 9) & 0x1u) << 6) |
                                                 (((op >> 3) & 0x1Fu) << 1));
    insn->r15_modified    = true;
    insn->r15_conditional = 1u;
    insn->und_in_it       = 1u;
    insn->place_fn        = &PlaceCbz;
    return true;
}

/* ARM DDI 0406C.c A8.8.54 IT encoding T1 (p. A8-390): firstcond = bits[7:4],
   mask = bits[3:0]; "if mask == '0000' then SEE Related encodings"; "if
   firstcond == '1111' || (firstcond == '1110' && BitCount(mask) != 1) then
   UNPREDICTABLE". */
bool ThumbStackControlDecoder::DecodeIfThen(DecodedInsn* insn, uint16_t op) {
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
bool ThumbStackControlDecoder::DecodeStackControlGroup(DecodedInsn* insn,
                                                       uint16_t op) {
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
           0001xxx/0011xxx/1001xxx/1011xxx to CBNZ, CBZ, variant v6T2. */
        if (!processor_config_->HasThumb2()) return false;
        return DecodeCompareAndBranch(insn, op);
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
