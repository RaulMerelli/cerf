#include "arm_jit_runtime.h"

#include "arm_cpu_exceptions.h"
#include "arm_jit.h"
#include "arm_mmu.h"
#include "arm_mmu_state.h"
#include "cpu_state.h"

extern "C" void* InterruptDeliveryHelper(ArmJit*      jit,
                                         ArmCpuState* state,
                                         uint32_t     target_pc) {
    return ArmCpuRaiseIrqException(jit, state, target_pc);
}
