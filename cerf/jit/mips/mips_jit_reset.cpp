#include "mips_jit.h"

#include "../../boot/rom_parser_service.h"
#include "../../core/cerf_emulator.h"
#include "../../cpu/mips_processor_config.h"
#include "../../host/guest_power_notifier.h"
#include "../../socs/guest_cpu_reset.h"

void MipsJit::SetResetPending(bool is_resume) {
    cpu_state_.reset_pending = 1;
    SignalIdleWake();   /* wake the JIT thread if parked so Run() delivers it */
    if (is_resume) { emu_.Get<GuestPowerNotifier>().NotifyResume(); return; }
    emu_.Get<GuestPowerNotifier>().NotifyReboot();
}

void MipsJit::NotifyResetDelivered() {
    emu_.Get<GuestCpuReset>().OnResetDelivered();
}

void MipsJit::DeliverReset() {
    /* Reset-line listeners + (hard reset only) the cold-boot RAM wipe/replay,
       which also flushes the whole translation cache. */
    NotifyResetDelivered();

    /* Re-establish the exact cold-power-on CPU state (the kernel entry does its
       own CP0 init), so the guest re-boots identically to first power-on. */
    cpu_state_ = MipsCpuState{};
    cpu_state_.cp0_prid   = cpu_config_->Prid();
    cpu_state_.nb_tlb     = cpu_config_->TlbSize();
    cpu_state_.tlb_in_use = cpu_state_.nb_tlb;
    cpu_state_.min_page_shift = cpu_config_->MinPageShift();
    cpu_state_.phys_addr_mask = cpu_config_->PhysAddrMask();
    cpu_state_.pc         = emu_.Get<RomParserService>().EntryVa();

    /* The reset zeroes the MMU (ASID 0), so the VA->native jump-cache shortcuts
       are stale; the phys-keyed block index stays valid for warm RAM. */
    blocks_.JumpCacheFlush();
}
