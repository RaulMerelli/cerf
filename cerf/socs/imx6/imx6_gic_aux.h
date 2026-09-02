#pragma once

#include "../../state/state_stream.h"

#include <cstdint>

namespace imx6_gic_detail {

/* Register offsets 0x000-0x054 / 0x200-0x218 / 0x620-0x634 relative to
   Imx6Gic::kMmioBase. */
class Imx6GicAux {
public:
    bool ReadMmio(uint32_t off, uint32_t& value) const {
        switch (off) {
        case 0x000: value = scu_control_; return true;
        case 0x004: value = 0u; return true;
        case 0x008: value = 0u; return true;
        case 0x030: value = scu_diag_control_; return true;
        case 0x040: value = scu_filter_start_; return true;
        case 0x044: value = scu_filter_end_; return true;
        case 0x050: value = scu_access_control_; return true;
        case 0x054: value = scu_ns_access_control_; return true;
        case 0x200: value = static_cast<uint32_t>(gt_base64_); return true;
        case 0x204: value = static_cast<uint32_t>(gt_base64_ >> 32); return true;
        case 0x208: value = global_timer_control_; return true;
        case 0x20C: value = global_timer_status_; return true;
        case 0x210: value = global_timer_compare_lo_; return true;
        case 0x214: value = global_timer_compare_hi_; return true;
        case 0x218: value = global_timer_increment_; return true;
        case 0x620: value = watchdog_load_; return true;
        case 0x624: value = watchdog_counter_; return true;
        case 0x628: value = watchdog_control_; return true;
        case 0x62C: value = watchdog_status_; return true;
        case 0x630: value = watchdog_reset_status_; return true;
        case 0x634: value = watchdog_disable_; return true;
        default: return false;
        }
    }

    bool WriteMmio(uint32_t off, uint32_t value) {
        switch (off) {
        case 0x000: scu_control_ = value & 1u; return true;
        case 0x00C: return true;
        case 0x030: scu_diag_control_ = value; return true;
        case 0x040: scu_filter_start_ = value; return true;
        case 0x044: scu_filter_end_ = value; return true;
        case 0x050: scu_access_control_ = value; return true;
        case 0x054: scu_ns_access_control_ = value; return true;
        case 0x200: gt_base64_ = (gt_base64_ & 0xFFFFFFFF00000000ull) | value; return true;
        case 0x204:
            gt_base64_ = (gt_base64_ & 0x00000000FFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
            return true;
        case 0x208: global_timer_control_ = value; return true;
        case 0x20C: global_timer_status_ &= ~value; return true;
        case 0x210: global_timer_compare_lo_ = value; return true;
        case 0x214: global_timer_compare_hi_ = value; return true;
        case 0x218: global_timer_increment_ = value; return true;
        case 0x620: watchdog_load_ = value; return true;
        case 0x624: watchdog_counter_ = value; return true;
        case 0x628: watchdog_control_ = value; return true;
        case 0x62C: watchdog_status_ &= ~value; return true;
        case 0x630: watchdog_reset_status_ &= ~value; return true;
        case 0x634: watchdog_disable_ = value; return true;
        default: return false;
        }
    }

    /* Cortex-A9 MPCore global timer.  Register roles and control-bit meanings
       as the Linux driver documents them
       (drivers/clocksource/arm_global_timer.c): counter at 0x00/0x04,
       GT_CONTROL bit 0 timer enable, bit 1 comparator enable, bit 2 IRQ
       enable, bit 3 auto-increment, bits[15:8] prescaler; GT_INT_STATUS bit 0
       is the event flag the handler tests and then writes back to clear; the
       comparator is at 0x10/0x14 and the auto-increment step at 0x18. */
    static constexpr uint32_t kGtEnable = 1u << 0;
    static constexpr uint32_t kGtCompEnable = 1u << 1;
    static constexpr uint32_t kGtIrqEnable = 1u << 2;
    static constexpr uint32_t kGtAutoInc = 1u << 3;
    static constexpr uint32_t kGtEventFlag = 1u << 0;

    void AdvanceGlobalTimer(uint32_t cycles_now, uint32_t periph_div) {
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

    /* The event flag only reaches the CPU while the banked IRQ enable is set. */
    bool GlobalTimerIrqPending() const {
        return (global_timer_status_ & kGtEventFlag) != 0u && (global_timer_control_ & kGtIrqEnable) != 0u;
    }

    void ResetGlobalTimerAnchor(uint32_t cycles_now) { gt_anchor_cycles_ = cycles_now; }

    void Reset() { *this = Imx6GicAux{}; }

    void SaveState(StateWriter& w) const {
        w.Write(gt_anchor_cycles_);
        w.Write(gt_base64_);
        w.Write(scu_control_);
        w.Write(scu_diag_control_);
        w.Write(scu_filter_start_);
        w.Write(scu_filter_end_);
        w.Write(scu_access_control_);
        w.Write(scu_ns_access_control_);
        w.Write(global_timer_control_);
        w.Write(global_timer_status_);
        w.Write(global_timer_compare_lo_);
        w.Write(global_timer_compare_hi_);
        w.Write(global_timer_increment_);
        w.Write(watchdog_load_);
        w.Write(watchdog_counter_);
        w.Write(watchdog_control_);
        w.Write(watchdog_status_);
        w.Write(watchdog_reset_status_);
        w.Write(watchdog_disable_);
    }

    void RestoreState(StateReader& r) {
        r.Read(gt_anchor_cycles_);
        r.Read(gt_base64_);
        r.Read(scu_control_);
        r.Read(scu_diag_control_);
        r.Read(scu_filter_start_);
        r.Read(scu_filter_end_);
        r.Read(scu_access_control_);
        r.Read(scu_ns_access_control_);
        r.Read(global_timer_control_);
        r.Read(global_timer_status_);
        r.Read(global_timer_compare_lo_);
        r.Read(global_timer_compare_hi_);
        r.Read(global_timer_increment_);
        r.Read(watchdog_load_);
        r.Read(watchdog_counter_);
        r.Read(watchdog_control_);
        r.Read(watchdog_status_);
        r.Read(watchdog_reset_status_);
        r.Read(watchdog_disable_);
    }

private:
    uint32_t scu_control_ = 0;
    uint32_t scu_diag_control_ = 0;
    uint32_t scu_filter_start_ = 0;
    uint32_t scu_filter_end_ = 0;
    uint32_t scu_access_control_ = 0;
    uint32_t scu_ns_access_control_ = 0;
    uint32_t gt_anchor_cycles_ = 0;
    uint64_t gt_base64_ = 0;
    uint32_t global_timer_control_ = 0;
    uint32_t global_timer_status_ = 0;
    uint32_t global_timer_compare_lo_ = 0;
    uint32_t global_timer_compare_hi_ = 0;
    uint32_t global_timer_increment_ = 0;
    uint32_t watchdog_load_ = 0;
    uint32_t watchdog_counter_ = 0;
    uint32_t watchdog_control_ = 0;
    uint32_t watchdog_status_ = 0;
    uint32_t watchdog_reset_status_ = 0;
    uint32_t watchdog_disable_ = 0;
};

} // namespace imx6_gic_detail
