#include "imx6_gic_aux.h"

#include "../../state/state_stream.h"

namespace imx6_gic_detail {

namespace {

/* Linux arm_global_timer.c defines counter, control, status, comparator,
   auto-increment offsets, control fields, and the write-one-to-clear event flag. */
constexpr uint32_t kGtEnable = 1u << 0;
constexpr uint32_t kGtCompEnable = 1u << 1;
constexpr uint32_t kGtIrqEnable = 1u << 2;
constexpr uint32_t kGtAutoInc = 1u << 3;
constexpr uint32_t kGtEventFlag = 1u << 0;

/* ARM DDI 0407F Table 2-3: SCU Configuration [9:8] hold the CPU0 tag RAM size, b01 = 32KB, and
   [7:4] the processors taking part in coherency; ARM DDI 0388I §4.3.10 puts that state in
   ACTLR[6]; IMX6DQRM Rev.2 Table 12-4 gives DCACHESIZE 32. */
constexpr uint32_t kScuConfigTagRam32Kb = 0x00000100u;
constexpr uint32_t kScuConfigCpu0Smp = 0x00000010u;
constexpr uint32_t kActlrSmp = 0x00000040u;

/* ARM DDI 0407F Table 2-2: SCU Control [1] routes a physical range to master port M1; Table 2-8
   gives each SCU Access Control bit one processor, all four set at reset. */
constexpr uint32_t kScuControlAddressFiltering = 0x00000002u;
constexpr uint32_t kScuAccessControlCpu0 = 0x00000001u;

}

bool Imx6GicAux::ScuRead(uint32_t off, uint32_t aux_control_register, uint32_t& value) const {
    switch (off) {
    case 0x000: value = scu_control_; return true;
    case 0x004:
        value = kScuConfigTagRam32Kb |
                ((aux_control_register & kActlrSmp) != 0u ? kScuConfigCpu0Smp : 0u);
        return true;
    /* ARM UAN 0005D erratum 764369: bit[0] of the undocumented SCU Diagnostic Control register
       at offset 0x30 disables the migratory bit feature, and the bit "can be written, but is
       always Read as Zero". */
    case 0x030: value = 0u; return true;
    case 0x050: value = scu_access_control_; return true;
    default: return false;
    }
}

bool Imx6GicAux::ScuWrite(uint32_t off, uint32_t value, const char*& unmodelled) {
    switch (off) {
    case 0x000:
        if ((value & kScuControlAddressFiltering) != 0u) unmodelled = "SCU address filtering";
        scu_control_ = value;
        return true;
    case 0x00C:
    case 0x030: return true;
    case 0x050:
        if ((value & kScuAccessControlCpu0) == 0u) unmodelled = "an SCU access control that locks CPU0 out";
        scu_access_control_ = value;
        return true;
    default: return false;
    }
}

bool Imx6GicAux::ReadMmio(uint32_t off, uint32_t& value) const {
    switch (off) {
    case 0x200: value = static_cast<uint32_t>(gt_base64_); return true;
    case 0x204: value = static_cast<uint32_t>(gt_base64_ >> 32); return true;
    case 0x208: value = global_timer_control_; return true;
    case 0x20C: value = global_timer_status_; return true;
    case 0x210: value = global_timer_compare_lo_; return true;
    case 0x214: value = global_timer_compare_hi_; return true;
    case 0x218: value = global_timer_increment_; return true;
    default: return false;
    }
}

bool Imx6GicAux::WriteMmio(uint32_t off, uint32_t value) {
    switch (off) {
    case 0x200: gt_base64_ = (gt_base64_ & 0xFFFFFFFF00000000ull) | value; return true;
    case 0x204:
        gt_base64_ = (gt_base64_ & 0x00000000FFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
        return true;
    case 0x208: global_timer_control_ = value; return true;
    case 0x20C: global_timer_status_ &= ~value; return true;
    case 0x210: global_timer_compare_lo_ = value; return true;
    case 0x214: global_timer_compare_hi_ = value; return true;
    case 0x218: global_timer_increment_ = value; return true;
    default: return false;
    }
}

void Imx6GicAux::AdvanceGlobalTimer(uint32_t cycles_now, uint32_t periph_div) {
    if ((global_timer_control_ & kGtEnable) == 0) {
        gt_anchor_cycles_ = cycles_now;
        return;
    }
    const uint32_t presc = ((global_timer_control_ >> 8) & 0xFFu) + 1u;
    const uint64_t cyc_per_tick = static_cast<uint64_t>(presc) * periph_div;
    if (cyc_per_tick == 0) return;
    const uint32_t elapsed = cycles_now - gt_anchor_cycles_;
    const uint64_t ticks = elapsed / cyc_per_tick;
    if (ticks != 0) {
        gt_base64_ += ticks;
        gt_anchor_cycles_ += static_cast<uint32_t>(ticks * cyc_per_tick);
    }

    if ((global_timer_control_ & kGtCompEnable) == 0) return;
    uint64_t compare = (static_cast<uint64_t>(global_timer_compare_hi_) << 32) | global_timer_compare_lo_;
    if (gt_base64_ < compare) return;
    global_timer_status_ |= kGtEventFlag;
    if ((global_timer_control_ & kGtAutoInc) != 0 && global_timer_increment_ != 0) {
        const uint64_t step = global_timer_increment_;
        compare += ((gt_base64_ - compare) / step + 1u) * step;
        global_timer_compare_lo_ = static_cast<uint32_t>(compare);
        global_timer_compare_hi_ = static_cast<uint32_t>(compare >> 32);
    }
}

bool Imx6GicAux::GlobalTimerIrqPending() const {
    return (global_timer_status_ & kGtEventFlag) != 0u && (global_timer_control_ & kGtIrqEnable) != 0u;
}

void Imx6GicAux::ResetGlobalTimerAnchor(uint32_t cycles_now) { gt_anchor_cycles_ = cycles_now; }

void Imx6GicAux::Reset() { *this = Imx6GicAux{}; }

void Imx6GicAux::SaveState(StateWriter& w) const {
    w.Write(scu_control_);
    w.Write(scu_access_control_);
    w.Write(gt_anchor_cycles_);
    w.Write(gt_base64_);
    w.Write(global_timer_control_);
    w.Write(global_timer_status_);
    w.Write(global_timer_compare_lo_);
    w.Write(global_timer_compare_hi_);
    w.Write(global_timer_increment_);
}

void Imx6GicAux::RestoreState(StateReader& r) {
    r.Read(scu_control_);
    r.Read(scu_access_control_);
    r.Read(gt_anchor_cycles_);
    r.Read(gt_base64_);
    r.Read(global_timer_control_);
    r.Read(global_timer_status_);
    r.Read(global_timer_compare_lo_);
    r.Read(global_timer_compare_hi_);
    r.Read(global_timer_increment_);
}

}
