#pragma once

#include <cstdint>

#include "../core/service.h"
#include "cpu_state.h"

class ArmJit;
struct DecodedInsn;

class ArmCpu : public Service {
public:
    using Service::Service;

    void OnReady() override;

    ArmCpuState* State() { return &state_; }

    /* Called by ArmJit::OnReady — wires the back-pointer to the
       owning ArmJit. Resolving ArmJit inside ArmCpu::OnReady would
       form a service-locator cycle and halt. */
    void LateInit(ArmJit* jit);

    void BankSwitch();

    uint32_t GetCpsrWithFlags() const;

    void UpdateFlags(uint32_t new_flag_value);

    void UpdateCpsrWithFlags(ArmPsrFull new_psr);

    void UpdateCpsr(ArmPsr new_psr);

    void* RaiseUndefinedException     (uint32_t inst_ptr);
    void* RaiseAbortDataException     (uint32_t inst_ptr);
    void* RaiseAbortPrefetchException (uint32_t inst_ptr);
    void* RaiseIrqException           (uint32_t inst_ptr);
    void* RaiseSoftwareInterruptException(uint32_t inst_ptr);

    /* Must run BEFORE RaiseResetException — the SVC bank slot
       must hold the SP value when post-reset BankSwitch rotates
       it into visible R13. */
    void SetInitialStackPointer(uint32_t sp);

    void RaiseResetException(uint32_t initial_pc);

    /* No-arg overload reuses cached initial_pc_ from the prior
       (uint32_t) overload; calling before any cold reset has
       cached a value lands the soft reset at PC=0. */
    void RaiseResetException();

    bool AreInterruptsEnabled() const;

    uint32_t* GetUserModeRegisterAddress(int reg_num);

    static void* __cdecl RaiseUndefinedExceptionHelper      (ArmCpu* cpu, uint32_t pc);
    static void* __cdecl RaiseAbortPrefetchExceptionHelper  (ArmCpu* cpu, uint32_t pc);
    static void* __cdecl RaiseSoftwareInterruptExceptionHelper(ArmCpu* cpu, uint32_t pc);

    static void __cdecl PerformSyscallHelper();

    static void __cdecl PowerDownHelper();

    static uint32_t ComputePSRMaskValue(int field_mask);

    static uint8_t GetX86FlagsMask(const DecodedInsn* d);

    static uint32_t __cdecl UpdatePSRMaskHelper(uint32_t current_psr,
                                                uint32_t new_psr,
                                                uint32_t mask,
                                                ArmCpu*  cpu);
    static uint32_t __cdecl GetCpsrWithFlagsHelper(ArmCpu* cpu);
    static void     __cdecl UpdateFlagsHelper(ArmCpu* cpu, uint32_t new_flags);
    static void     __cdecl UpdateNzcvOnlyHelper(ArmCpu* cpu, uint32_t new_flags);
    static void     __cdecl UpdateCpsrWithFlagsHelper(ArmCpu* cpu, uint32_t new_psr_word);

private:
    void DoRaiseReset();

    ArmCpuState     state_{};
    class ArmMmu*   mmu_ = nullptr;
    ArmJit*         jit_ = nullptr;

    uint32_t        initial_pc_ = 0;
};
