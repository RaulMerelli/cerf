#include "arm_jit_runtime.h"

#include "arm_jit.h"
#include "arm_mmu.h"
#include "arm_mmu_state.h"
#include "cpu_state.h"

extern "C" void* InterruptDeliveryHelper(ArmJit*      jit,
                                         ArmCpuState*,
                                         uint32_t     target_pc) {
    return ArmJit::RaiseIrqExceptionHelper(target_pc, jit);
}
