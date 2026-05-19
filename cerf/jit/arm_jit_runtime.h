#pragma once

#include <cstdint>

class ArmJit;
struct ArmCpuState;

extern "C" void* InterruptDeliveryHelper(ArmJit*       jit,
                                         ArmCpuState*  state,
                                         uint32_t      target_pc);
