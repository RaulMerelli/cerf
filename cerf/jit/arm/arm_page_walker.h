#pragma once

#include <cstdint>

#include "../../core/service.h"
#include "arm_mmu_state.h"
#include "cpu_state.h"

class ArmInjectionBand;
class ArmMmu;
class ArmProcessorConfig;
class EmulatedMemory;

class ArmPageWalker : public Service {
public:
    using Service::Service;

    void OnReady() override;
    bool ShouldRegister() override;

    uint8_t* TranslateRead     (ArmCpuState* cpu_state, uint32_t va);
    uint8_t* TranslateWrite    (ArmCpuState* cpu_state, uint32_t va);
    uint8_t* TranslateReadWrite(ArmCpuState* cpu_state, uint32_t va);
    uint8_t* TranslateExecute  (ArmCpuState* cpu_state, uint32_t va);

    /* LDRT/STRT-class accesses: "the memory system is signaled to treat the
       access as if the processor were in User mode" (ARM DDI 0100I A4.1.25
       p. A4-48 / A4.1.31 p. A4-60 / A4.1.101 p. A4-197 / A4.1.105
       p. A4-206; ARM DDI 0406C.c A8.8.92). */
    uint8_t* TranslateUserRead (ArmCpuState* cpu_state, uint32_t va);
    uint8_t* TranslateUserWrite(ArmCpuState* cpu_state, uint32_t va);

    uint32_t LastExecPa() const { return last_exec_pa_; }

private:
    template <ArmMmuAccess kAccess, bool kForceUser = false>
    uint8_t* MapGuestVirtualToHost(ArmCpuState* cpu_state, uint32_t p);

    ArmMmu*             mmu_              = nullptr;
    ArmMmuState*        state_p_          = nullptr;
    EmulatedMemory*     memory_           = nullptr;
    ArmProcessorConfig* processor_config_ = nullptr;
    ArmInjectionBand*   injection_band_   = nullptr;

    uint32_t last_exec_pa_ = 0;
};
