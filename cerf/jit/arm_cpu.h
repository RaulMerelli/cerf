#pragma once

#include <cstdint>

#include "../core/service.h"
#include "cpu_state.h"

class ArmJit;
struct DecodedInsn;
class StateWriter;
class StateReader;

class ArmCpu : public Service {
public:
    using Service::Service;

    void OnReady() override;

    ArmCpuState* State() { return &state_; }

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

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

    /* No-arg overload: uses the pending resume vector (SetPendingResumeVector)
       if set, else the cached initial_pc_; never-cached lands the reset at PC=0. */
    void RaiseResetException();

    /* Override the next reset delivery's PC with a SoC sleep-wake resume vector
       (SA-1110: the PSPR vector its boot ROM jumps to on an SMR wake). One-shot. */
    void SetPendingResumeVector(uint32_t pc);

    /* A power-off-wake resume re-enters the OS mid-execution with the MMU live, so
       the reset delivery reinstates the saved cp15 c1/c2/c3 (S3C2410/DevEmu: the
       values EBOOT restores from SLEEPDATA, startup.s wakeup routine). One-shot;
       paired with SetPendingResumeVector. */
    void SetPendingResumeMmu(uint32_t control, uint32_t ttbr0, uint32_t dacr);

    bool AreInterruptsEnabled() const;

    uint32_t* GetUserModeRegisterAddress(int reg_num);

    static void* __cdecl RaiseUndefinedExceptionHelper      (ArmCpu* cpu, uint32_t pc);
    static void* __cdecl RaiseAbortPrefetchExceptionHelper  (ArmCpu* cpu, uint32_t pc);
    static void* __cdecl RaiseSoftwareInterruptExceptionHelper(ArmCpu* cpu, uint32_t pc);

    static void __cdecl PerformSyscallHelper();

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

    uint32_t        pending_resume_pc_     = 0;
    bool            has_pending_resume_pc_ = false;

    uint32_t        pending_resume_mmu_control_ = 0;
    uint32_t        pending_resume_mmu_ttbr0_   = 0;
    uint32_t        pending_resume_mmu_dacr_    = 0;
    bool            has_pending_resume_mmu_     = false;
};
