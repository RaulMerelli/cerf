#include "mips_jit.h"

#include "../../cpu/emulated_memory.h"
#include "../../state/state_stream.h"

/* MipsCpuState is a flat integer POD (GPRs + CP0 + the software TLB + the
   in-core guest-cycle timer), no host pointers/handles, so a whole-struct blob
   is consistent: count_anchor and guest_cycle_counter are saved together and
   the timer is pure guest-cycle, so TimerPoll resumes with no rebase. */
void MipsJit::SaveCpuState(StateWriter& w)    { w.Write(cpu_state_); }
void MipsJit::RestoreCpuState(StateReader& r) { r.Read(cpu_state_); }

void MipsJit::SaveMmuState(StateWriter& w)    { mmu_->SaveState(w); }
void MipsJit::RestoreMmuState(StateReader& r) { mmu_->RestoreState(r); }

/* No-op: Run() re-folds the INTC-driven external_ip_ into cp0_cause and
   re-checks InterruptReady() every iteration, and the INTC PostRestore
   re-drives external_ip_, so no poll byte needs re-arming after a restore. */
void MipsJit::ResyncInterruptPoll() {}

void MipsJit::FlushTranslationCache(uint32_t /*va*/, uint32_t /*length*/) {
    arena_.Flush();
    blocks_.FlushAll();
}

void MipsJit::SetInjectionBand(uint32_t va, uint32_t pa, uint32_t size) {
    mmu_->SetInjectionBand(va, pa, size);
    band_host_base_ = memory_->TryTranslateWrite(pa);
    band_size_      = band_host_base_ ? size : 0u;
}
