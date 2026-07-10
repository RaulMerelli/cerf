#include "mips_jit.h"

#include "mips_cpu_state.h"
#include "mips_exception_model.h"

/* In-core R4000 Count/Compare timer + CP0 interrupt delivery (the CE scheduler
   tick). Reimplemented from QEMU target/mips cp0_timer.c, driven off
   guest_cycle_counter on the JIT thread (no host timer / worker) and polled at
   the top of Run(); per-function citations below. */

namespace {
/* IP7 = Cause bit 15. The R4000/VR5500 timer interrupt is hardwired to the
   highest hardware interrupt line, IP7 (pre-Release-2: no IntCtl.IPTI). */
constexpr uint32_t kCauseIp7 = 1u << (MipsCauseBit::kIP + 7);
}  // namespace

void MipsJit::TimerPoll() {
    MipsCpuState& s = cpu_state_;
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

void __fastcall MipsJit::Mtc0CountHelper(uint32_t value, MipsJit* jit) {
    /* store_count: set Count and re-anchor so the next poll's elapsed is measured
       from here. */
    MipsCpuState& s = jit->cpu_state_;
    s.cp0_count    = value;
    s.count_anchor = s.guest_cycle_counter;
}

void __fastcall MipsJit::Mtc0CompareHelper(uint32_t value, MipsJit* jit) {
    /* store_compare: set Compare, lower the pending timer IRQ (IP7), and re-arm
       for the next crossing. */
    MipsCpuState& s = jit->cpu_state_;
    s.cp0_compare = value;
    s.cp0_cause  &= ~kCauseIp7;
    s.timer_armed = 1;
}

bool MipsJit::InterruptReady() const {
    const MipsCpuState& s = cpu_state_;
    if (!exception_->InterruptsEnabled(s)) return false;
    /* pending = (Cause.IP & Status.IM) over bits 8..15 (internal.h). Both cores
       place IntMask and the Int/Sw pending bits at 15:8 (TMPR39xx-um §6.2.3). */
    return (s.cp0_cause & s.cp0_status & 0x0000FF00u) != 0u;
}

void MipsJit::DeliverInterrupt() {
    /* EXCP_INT -> ExcCode 0, general vector 0x180; EPC = the interrupted pc (the
       block boundary; the caller has already ensured branch_state == kNone, so
       EnterException records EPC without the branch-delay adjustment). No SEH. */
    EnterException(MipsExcCode::kInt, false);
}
