#include "arm_branch_block_space_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "arm_opcode.h"
#include "decoded_insn.h"
#include "place_fns.h"

REGISTER_SERVICE(ArmBranchBlockSpaceDecoder);

bool ArmBranchBlockSpaceDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

/* DDI 0406C.c Table A5-21 (p. A5-214): op = insn[25:20]; 0xxxxx LDM/STM
   incl. the 0xx1x0 / 0xx1x1 user-register and exception-return forms,
   10xxxx B (p. A8-334), 11xxxx BL (p. A8-348) - all rows are in all
   architecture variants. */
bool ArmBranchBlockSpaceDecoder::Decode(DecodedInsn* insn, ArmOpcode op) {
    if (((op.word >> 25) & 0x1u) != 0u) {
        /* A8.8.18 B / A8.8.25 BL encoding A1 (pp. A8-334/A8-348):
           imm32 = SignExtend(imm24:'00', 32), L = insn[24]. */
        insn->l            = (op.word >> 24) & 0x1u;
        insn->offset       = static_cast<int32_t>(op.word << 8) >> 6;
        insn->r15_modified = true;
        insn->place_fn     = &PlaceBranch;
        return true;
    }
    insn->p             = (op.word >> 24) & 0x1u;
    insn->u             = (op.word >> 23) & 0x1u;
    insn->s             = (op.word >> 22) & 0x1u;
    insn->w             = (op.word >> 21) & 0x1u;
    insn->l             = (op.word >> 20) & 0x1u;
    insn->rn            = (op.word >> 16) & 0xFu;
    insn->register_list = static_cast<uint16_t>(op.word & 0xFFFFu);
    const bool pc_load  = insn->l != 0u &&
                          (insn->register_list & 0x8000u) != 0u;
    insn->r15_modified        = pc_load;
    insn->is_exception_return = pc_load && insn->s != 0u;
    insn->place_fn            = &PlaceBlockDataTransfer;
    return true;
}
