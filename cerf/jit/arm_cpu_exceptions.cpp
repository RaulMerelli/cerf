#include "arm_cpu_exceptions.h"

#include "arm_cpu_ops.h"
#include "arm_jit.h"
#include "arm_mmu.h"
#include "arm_mmu_state.h"

#include "../core/log.h"

namespace {

inline void* EnterException(ArmJit*         jit,
                            ArmCpuState*    state,
                            uint32_t        target_mode,
                            uint32_t        vector_offset,
                            uint32_t        lr_value,
                            ExceptionVector cache_slot) {
    /* Clear any armed LDREX exclusive monitor. DDI 0100I §LDREX
       line 3042 documents that an LDREX/STREX pair only succeeds
       when no context-changing event intervenes; Cortex-A8
       silicon clears the monitor on every exception entry. */
    state->ldrex_monitor_armed = 0;

    const uint32_t old_cpsr_full = ArmCpuGetCpsrWithFlags(state);

    ArmPsr new_psr;
    new_psr.partial_word     = old_cpsr_full & 0x0FFFFFFFu;
    new_psr.bits.mode        = target_mode;
    new_psr.bits.thumb_mode  = 0;
    new_psr.bits.irq_disable = 1;
    ArmCpuUpdateCpsr(jit, state, new_psr);

    /* state->spsr now references the new mode's bank slot (the
       BankSwitch inside ArmCpuUpdateCpsr rotated it into place). */
    state->spsr.word = old_cpsr_full;

    state->gprs[ArmGpr::kR14] = lr_value;

    const ArmMmuState* mmu = jit->Mmu()->State();
    const bool high_vectors =
        mmu->control_register.bits.m && mmu->control_register.bits.v;
    state->gprs[ArmGpr::kR15] =
        vector_offset | (high_vectors ? 0xFFFF0000u : 0u);

    /* Cache lookup. The cached pointer is invalidated by
       FlushNativeAddrCache whenever the translation cache flushes,
       so a stale slot can never point into freed code. */
    if (void* cached = jit->NativeAddr(cache_slot)) {
        return cached;
    }
    void* native = jit->FindBlockNativeStart(state->gprs[ArmGpr::kR15]);
    if (native) {
        jit->SetNativeAddr(cache_slot, native);
    }
    return native;
}

}  // namespace

void* ArmCpuRaiseUndefinedException(ArmJit* jit, ArmCpuState* state, uint32_t inst_ptr) {
    /* LR = faulting PC + 2 in Thumb, +4 in ARM. */
    const uint32_t lr = inst_ptr + (state->cpsr.bits.thumb_mode ? 2u : 4u);
    return EnterException(jit, state, ArmMode::kUndefined, 0x04u, lr,
                          ExceptionVector::kUndef);
}

void* ArmCpuRaiseAbortDataException(ArmJit* jit, ArmCpuState* state, uint32_t inst_ptr) {
    /* Data abort: LR = faulting PC + 8 (re-execute via SUBS PC, LR, #8). */
    return EnterException(jit, state, ArmMode::kAbort, 0x10u, inst_ptr + 8u,
                          ExceptionVector::kAbortData);
}

void* ArmCpuRaiseAbortPrefetchException(ArmJit* jit, ArmCpuState* state, uint32_t inst_ptr) {
    /* Prefetch abort: LR = faulting PC + 4 (SUBS PC, LR, #4 to re-fetch). */
    return EnterException(jit, state, ArmMode::kAbort, 0x0Cu, inst_ptr + 4u,
                          ExceptionVector::kAbortPrefetch);
}

void* ArmCpuRaiseIrqException(ArmJit* jit, ArmCpuState* state, uint32_t inst_ptr) {
    /* Soft-reset is multiplexed onto IRQ delivery — dropping this
       branch silently breaks watchdog / OAL CPU-reset. */
    if (state->reset_pending) {
        jit->Cpu()->RaiseResetException();
        return nullptr;
    }

    return EnterException(jit, state, ArmMode::kIrq, 0x18u, inst_ptr + 4u,
                          ExceptionVector::kIrq);
}

void* ArmCpuRaiseSoftwareInterruptException(ArmJit* jit, ArmCpuState* state, uint32_t inst_ptr) {
    /* SWI: LR = next instruction (SWI's PC + 2 in Thumb, +4 in ARM). */
    const uint32_t lr = inst_ptr + (state->cpsr.bits.thumb_mode ? 2u : 4u);
    return EnterException(jit, state, ArmMode::kSupervisor, 0x08u, lr,
                          ExceptionVector::kSwi);
}
