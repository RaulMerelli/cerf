#include "arm_decoder.h"

#include "../cpu/arm_processor_config.h"
#include "arm_opcode.h"
#include "decoded_insn.h"
#include "place_fns.h"

bool ArmDecoder::DecodeArmUnconditional(DecodedInsn* insn, ArmOpcode op) {
    {
        const uint32_t masked = op.unconditional_extension.opcode1 & 0xD7u;
        if ((op.unconditional_extension.x2 & 0x0F0u) == 0x0F0u &&
            (masked == 0x55u || masked == 0x45u)) {
            insn->place_fn = &PlaceNop;
            insn->cond     = 14;
            return true;
        }
    }

    /* ARMv7 DMB / DSB / ISB barriers — NOP emit on x86's strong
       memory model. Encoding bits per
       references/omap3530/armv7_arch_excerpts.txt § DMB/DSB/ISB. */
    if (processor_config_->HasBarrierInsn() &&
        op.unconditional_extension.opcode1 == 0x57u &&
        op.unconditional_extension.x2      == 0xFF0u &&
        op.unconditional_extension.opcode2 >= 0x4u &&
        op.unconditional_extension.opcode2 <= 0x6u) {
        insn->place_fn = &PlaceNop;
        insn->cond     = 14;
        return true;
    }

    return false;
}
