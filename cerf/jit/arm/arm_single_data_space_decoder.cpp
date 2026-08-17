#include "arm_single_data_space_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "arm_opcode.h"
#include "decoded_insn.h"
#include "place_fns.h"

REGISTER_SERVICE(ArmSingleDataSpaceDecoder);

bool ArmSingleDataSpaceDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

/* DDI 0406C.c Table A5-15 (p. A5-208): A = insn[25], op1 = insn[24:20]
   = P/U/B/W/L, Rn = insn[19:16]; "These instructions have either A == 0 or
   B == 0. For instructions with A == 1 and B == 1, see Media instructions on
   page A5-209." */
bool ArmSingleDataSpaceDecoder::Decode(DecodedInsn* insn, ArmOpcode op) {
    insn->p  = (op.word >> 24) & 0x1u;
    insn->u  = (op.word >> 23) & 0x1u;
    insn->s  = (op.word >> 22) & 0x1u;
    insn->w  = (op.word >> 21) & 0x1u;
    insn->l  = (op.word >> 20) & 0x1u;
    insn->rn = (op.word >> 16) & 0xFu;
    insn->rd = (op.word >> 12) & 0xFu;
    if (((op.word >> 25) & 0x1u) != 0u) {
        insn->n   = 0u;
        insn->rm  = op.word & 0xFu;
        insn->op1 = (op.word >> 5) & 0x3u;
        insn->rs  = (op.word >> 7) & 0x1Fu;
    } else {
        insn->n = 1u;
        const uint32_t imm12 = op.word & 0xFFFu;
        insn->offset = insn->u != 0u ? static_cast<int32_t>(imm12)
                                     : -static_cast<int32_t>(imm12);
    }
    /* Table A5-15 (p. A5-208) op1 rows 0x010 / 0x011 / 0x110 / 0x111. */
    insn->unpriv = (insn->p == 0u && insn->w != 0u) ? 1u : 0u;
    insn->r15_modified =
        insn->l != 0u && insn->rd == 15u && insn->s == 0u;
    insn->place_fn = &PlaceSingleDataTransfer;
    return true;
}
