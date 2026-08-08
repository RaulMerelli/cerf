#include "arm_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../cpu/arm_processor_config.h"
#include "arm_branch_block_space_decoder.h"
#include "arm_coproc_space_decoder.h"
#include "arm_dataproc_space_decoder.h"
#include "arm_media_space_decoder.h"
#include "arm_opcode.h"
#include "arm_single_data_space_decoder.h"
#include "arm_unconditional_space_decoder.h"
#include "decoded_insn.h"
#include "place_fns.h"

REGISTER_SERVICE(ArmDecoder);

bool ArmDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void ArmDecoder::OnReady() {
    processor_config_      = &emu_.Get<ArmProcessorConfig>();
    branch_block_decoder_  = &emu_.Get<ArmBranchBlockSpaceDecoder>();
    coproc_decoder_        = &emu_.Get<ArmCoprocSpaceDecoder>();
    dataproc_decoder_      = &emu_.Get<ArmDataprocSpaceDecoder>();
    media_decoder_         = &emu_.Get<ArmMediaSpaceDecoder>();
    single_data_decoder_   = &emu_.Get<ArmSingleDataSpaceDecoder>();
    unconditional_decoder_ = &emu_.Get<ArmUnconditionalSpaceDecoder>();
}

/* Table A5-1 (ARM DDI 0406C.c A5.1, p. A5-194): cond + op1 = insn[27:25] +
   op = insn[4] select the instruction class. */
bool ArmDecoder::DecodeArm(DecodedInsn* insn, ArmOpcode op) {
    const uint32_t cond = op.word >> 28;

    if (cond == 0xFu) {
        insn->cond = 14u;
        /* ARM DDI 0100I A3.2.1 (p. A3-4): "In ARMv4, any instruction with
           a condition field of 0b1111 is UNPREDICTABLE." */
        if (!processor_config_->HasArmv5UnconditionalSpace()) {
            return MarkArmUnimplemented(insn, op.word);
        }
        return unconditional_decoder_->Decode(insn, op);
    }
    insn->cond = cond;

    switch ((op.word >> 25) & 0x7u) {
    case 0u:
    case 1u:
        /* DDI 0406C.c Table A5-2, p. A5-196. */
        return dataproc_decoder_->Decode(insn, op);
    case 2u:
        /* DDI 0406C.c Table A5-15, p. A5-208. */
        return single_data_decoder_->Decode(insn, op);
    case 3u:
        if (((op.word >> 4) & 0x1u) == 0u) {
            /* DDI 0406C.c Table A5-15, p. A5-208. */
            return single_data_decoder_->Decode(insn, op);
        }
        /* DDI 0406C.c Table A5-16, p. A5-209. */
        return media_decoder_->Decode(insn, op);
    case 4u:
    case 5u:
        /* DDI 0406C.c Table A5-21, p. A5-214. */
        return branch_block_decoder_->Decode(insn, op);
    default:
        /* DDI 0406C.c Table A5-22, p. A5-215. */
        return coproc_decoder_->Decode(insn, op);
    }
}
