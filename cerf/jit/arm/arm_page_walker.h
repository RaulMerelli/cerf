#pragma once

#include <cstdint>

#include "../../core/service.h"
#include "arm_mmu_state.h"
#include "cpu_state.h"

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

    void SetInjectionBand(uint32_t va_base, uint32_t pa_base, uint32_t size);

private:
    template <ArmMmuAccess kAccess, bool kForceUser = false>
    uint8_t* MapGuestVirtualToHost(ArmCpuState* cpu_state, uint32_t p);

    uint8_t* ServeInjectionBand(uint32_t va, ArmMmuAccess access);

    ArmMmu*             mmu_              = nullptr;
    ArmMmuState*        state_p_          = nullptr;
    EmulatedMemory*     memory_           = nullptr;
    ArmProcessorConfig* processor_config_ = nullptr;

    uint32_t last_exec_pa_ = 0;

    uint32_t injection_band_va_   = 0;
    uint32_t injection_band_pa_   = 0;
    uint32_t injection_band_size_ = 0;
};
