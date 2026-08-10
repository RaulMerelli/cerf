#include "mips_cp0_ops.h"

#include "../../core/cerf_emulator.h"
#include "../../cpu/mips_processor_config.h"
#include "mips_cpu.h"
#include "mips_cpu_state.h"
#include "mips_mmu.h"
#include "mips_translation_cache.h"

REGISTER_SERVICE(MipsCp0Ops);

namespace {
/* IP7 = Cause bit 15. The R4000/VR5500 timer interrupt is hardwired to the
   highest hardware interrupt line, IP7 (pre-Release-2: no IntCtl.IPTI). */
constexpr uint32_t kCauseIp7 = 1u << (MipsCauseBit::kIP + 7);
}

void MipsCp0Ops::OnReady() {
    cpu_state_ = emu_.Get<MipsCpu>().State();
    mmu_       = &emu_.Get<MipsMmu>();
    cache_     = &emu_.Get<MipsTranslationCache>();
    config_    = &emu_.Get<MipsProcessorConfig>();
}

void MipsCp0Ops::TimerPoll() {
    MipsCpuState& s = *cpu_state_;
    /* Count is free-running: advance it by the guest cycles elapsed since the
       last poll (uint32 wrap is the architectural Count wrap). cpu_mips_get_count
       / store_count keep Count = base + elapsed; here the field IS the live Count,
       refreshed each block. */
    const uint32_t now = s.guest_cycle_counter;
    s.cp0_count += now - s.count_anchor;
    s.count_anchor = now;

    /* Count == Compare fires once per Compare write (cpu_mips_timer_expire); IP7
       then stays asserted until software writes Compare again. The signed delta
       detects the crossing across the poll interval. */
    if (s.timer_armed &&
        static_cast<int32_t>(s.cp0_count - s.cp0_compare) >= 0) {
        s.cp0_cause |= kCauseIp7;
        s.timer_armed = 0;
    }
}

void __fastcall MipsCp0Ops::TlbwiHelper(MipsCp0Ops* ops) {
    ops->mmu_->WriteIndexed(ops->cpu_state_);
}

void __fastcall MipsCp0Ops::TlbwrHelper(MipsCp0Ops* ops) {
    ops->mmu_->WriteRandom(ops->cpu_state_);
}

void __fastcall MipsCp0Ops::TlbpHelper(MipsCp0Ops* ops) {
    ops->mmu_->Probe(ops->cpu_state_);
}

void __fastcall MipsCp0Ops::TlbrHelper(MipsCp0Ops* ops) {
    ops->mmu_->Read(ops->cpu_state_);
}

uint32_t __fastcall MipsCp0Ops::Mfc0RandomHelper(MipsCp0Ops* ops) {
    return ops->mmu_->RandomIndex(ops->cpu_state_);
}

void __fastcall MipsCp0Ops::Mtc0CountHelper(uint32_t value, MipsCp0Ops* ops) {
    /* store_count: set Count and re-anchor so the next poll's elapsed is measured
       from here. */
    MipsCpuState& s = *ops->cpu_state_;
    s.cp0_count    = value;
    s.count_anchor = s.guest_cycle_counter;
}

void __fastcall MipsCp0Ops::Mtc0CompareHelper(uint32_t value, MipsCp0Ops* ops) {
    /* store_compare: set Compare, lower the pending timer IRQ (IP7), and re-arm
       for the next crossing. */
    MipsCpuState& s = *ops->cpu_state_;
    s.cp0_compare = value;
    s.cp0_cause  &= ~kCauseIp7;
    s.timer_armed = 1;
}

void __fastcall MipsCp0Ops::Mtc0EntryHiHelper(uint32_t value, MipsCp0Ops* ops) {
    /* helper_mtc0_entryhi (cp0_helper.c:1142): write VPN2+ASID, preserve the
       reserved field, flush on an ASID change. mask = VPN2(VA[31:S+1]) | ASID. */
    MipsCpuState& s = *ops->cpu_state_;
    const uint32_t kMask = MipsVpn2Mask(s.min_page_shift) | 0xFFu;
    const uint32_t old = s.cp0_entryhi;
    const uint32_t val = (value & kMask) | (old & ~kMask);
    s.cp0_entryhi = val;
    if ((old & 0xFFu) != (val & 0xFFu)) {
        ops->cache_->ContextSwitchFlush();
    }
}

void __fastcall MipsCp0Ops::EretHelper(MipsCp0Ops* ops) {
    MipsCpuState& s = *ops->cpu_state_;
    /* MIPS64 Vol2 ERET: ERL takes precedence over EXL. */
    if ((s.cp0_status >> MipsStatusBit::kERL) & 1u) {
        s.pc = s.cp0_errorepc;
        s.cp0_status &= ~(1u << MipsStatusBit::kERL);
    } else {
        s.pc = s.cp0_epc;
        s.cp0_status &= ~(1u << MipsStatusBit::kEXL);
    }
    /* "The ERET instruction loads the ISA mode from bit 0 of the EPC or
       ErrorEPC register" when MIPS16 is enabled (U15509EJ2V0UM 3.4.3). */
    if (ops->config_->HasMips16()) {
        s.isa_mode = s.pc & 1u;
        s.pc &= ~1u;
    }
    s.llbit = 0;
}

void __fastcall MipsCp0Ops::RfeHelper(MipsCp0Ops* ops) {
    MipsCpuState& s = *ops->cpu_state_;
    /* IEc<-IEp, KUc<-KUp, IEp<-IEo, KUp<-KUo; IEo and KUo retain their values
       (TMPR39xx-um Fig 6-7). Status bits: IEc<0> KUc<1> IEp<2> KUp<3> IEo<4>
       KUo<5> (TMPR39xx-um §6.2.3). The Cache register's auto-lock half of RFE
       moves bits the TX39 CP0 register file does not surface. */
    s.cp0_status = (s.cp0_status & ~0xFu) | ((s.cp0_status >> 2) & 0xFu);
}
