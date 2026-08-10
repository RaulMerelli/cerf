#include "mips_cpu.h"

#include "../../boot/rom_parser_service.h"
#include "../../core/cerf_emulator.h"
#include "../../cpu/mips_processor_config.h"
#include "../../host/guest_deep_sleep.h"
#include "../../state/state_stream.h"

REGISTER_SERVICE(MipsCpu);

void MipsCpu::OnReady() { ResetState(); }

void MipsCpu::ResetState() {
    auto& cfg = emu_.Get<MipsProcessorConfig>();

    state_ = MipsCpuState{};
    state_.cp0_prid       = cfg.Prid();
    state_.nb_tlb         = cfg.TlbSize();
    state_.tlb_in_use     = state_.nb_tlb;
    state_.min_page_shift = cfg.MinPageShift();
    state_.phys_addr_mask = cfg.PhysAddrMask();

    state_.pc = emu_.Get<RomParserService>().EntryVa();
}

void __fastcall MipsCpu::HibernateHelper(uint32_t next_pc, MipsCpu* cpu) {
    cpu->state_.pc = next_pc;
    cpu->emu_.Get<GuestDeepSleep>().Enter();
}

void MipsCpu::SaveState(StateWriter& w)    { w.Write(state_); }
void MipsCpu::RestoreState(StateReader& r) { r.Read(state_); }
