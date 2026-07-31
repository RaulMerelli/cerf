#include "arm_processor_config.h"

#include "../jit/arm/decoded_insn.h"

uint16_t ArmProcessorConfig::CycleCostFor(const DecodedInsn& /*d*/) const {
    /* Base default: every instruction is 1 issue cycle. Concretes
       override with their chip's instruction-timing table. */
    return 1;
}
