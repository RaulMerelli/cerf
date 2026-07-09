#pragma once

#include <cstdint>
#include <optional>

#include "../core/service.h"

class StateWriter;
class StateReader;

/* The guest CPU engine JitRunner drives. ArmJit and MipsJit implement it; the
   active engine registers itself in OnReady (Provide<GuestEngine>) and JitRunner
   resolves Get<GuestEngine>(). */
class GuestEngine : public Service {
public:
    using Service::Service;

    virtual void     Run()                = 0;
    virtual bool     DeepSleep()    const = 0;
    virtual bool     ResetPending() const = 0;
    virtual uint32_t Pc()           const = 0;
    virtual void     DispatchTraceIter()  = 0;

    virtual std::optional<uint8_t*> PeekGuestVa(uint32_t va) = 0;

    /* ISA-neutral hibernation seam (the Cpu/Mmu .img sections route here). */
    virtual void SaveCpuState(StateWriter& w)    = 0;
    virtual void RestoreCpuState(StateReader& r) = 0;
    virtual void SaveMmuState(StateWriter& w)    = 0;
    virtual void RestoreMmuState(StateReader& r) = 0;

    virtual void ResyncInterruptPoll() = 0;
    virtual void FlushTranslationCache(uint32_t va, uint32_t length) = 0;

    /* Pend a CPU reset (GuestCpuReset/GuestColdBoot route here). is_resume
       selects the deep-sleep-wake notification over the reboot one. */
    virtual void SetResetPending(bool is_resume) = 0;

    /* Halt the guest CPU for deep sleep; JitRunner::RunLoop observes DeepSleep()
       and parks until a reset is pended. GuestDeepSleep::Enter routes here. */
    virtual void EnterDeepSleep() = 0;

    /* Apply a board's sleep-wake resume vector before the pending reset is
       delivered. The MMU triple is the ARM cp15 c1/c2/c3 block; an engine whose
       boards register no SleepResumeVectorProvider never receives this. */
    virtual void SetPendingResume(uint32_t pc, bool restore_mmu,
                                  uint32_t mmu_control, uint32_t ttbr0,
                                  uint32_t dacr) = 0;

    virtual void SetInjectionBand(uint32_t va, uint32_t pa, uint32_t size) = 0;

    virtual uint8_t* ResolveGuestVaToHost(uint32_t va) = 0;

    /* Highest addressable physical bit pattern. A SoC whose physical space
       mirrors high addresses into a smaller region returns the reduced mask;
       a flat space returns all-ones. */
    virtual uint32_t PhysAddrMask() const { return 0xFFFFFFFFu; }
};
