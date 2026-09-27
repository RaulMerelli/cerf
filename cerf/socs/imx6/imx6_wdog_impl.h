#pragma once

#include "../freescale_wdog_impl.h"

#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../core/virtual_clock.h"
#include "../../core/virtual_timer_list.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"
#include "imx6_gic.h"

#include <mutex>
#include "imx6_id.h"

namespace cerf_imx6_wdog_detail {

using cerf_freescale_wdog_detail::FreescaleWdogBase;
using cerf_freescale_wdog_detail::kWcr;
using cerf_freescale_wdog_detail::kWcrReset;
using cerf_freescale_wdog_detail::kWicr;
using cerf_freescale_wdog_detail::kWmcr;
using cerf_freescale_wdog_detail::kWrsr;
using cerf_freescale_wdog_detail::kWsr;

constexpr uint16_t kWicrReset = 0x0004u;

constexpr uint16_t kWrsrPor = 0x0010u;

template <uint32_t Base> class Imx6WdogBase : public FreescaleWdogBase<Base, SocId::Imx6> {
public:
    using Parent = FreescaleWdogBase<Base, SocId::Imx6>;
    using Parent::Parent;

    void OnReady() override {
        Parent::OnReady();
        interrupt_timer_ = this->emu_.Get<VirtualTimerList>().Add([this] { OnInterrupt(); });
        timer_ = this->emu_.Get<VirtualTimerList>().Add([this] { OnTimeout(); });
        this->emu_.Get<GuestCpuReset>().RegisterResetKindListener(
            [this](ResetKind kind) { ResetRegisters(kind); });
    }

    uint8_t ReadByte(uint32_t addr) override {
        std::lock_guard<std::mutex> lock(mtx_);
        return Parent::ReadByte(addr);
    }
    uint16_t ReadHalf(uint32_t addr) override {
        std::lock_guard<std::mutex> lock(mtx_);
        return Parent::ReadHalf(addr);
    }
    uint32_t ReadWord(uint32_t addr) override {
        std::lock_guard<std::mutex> lock(mtx_);
        return Parent::ReadWord(addr);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        bool reset = false;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            Parent::WriteByte(addr, value);
            reset = TakeResetRequestLocked();
        }
        DeliverResetRequest(reset);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        bool reset = false;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            Parent::WriteHalf(addr, value);
            reset = TakeResetRequestLocked();
        }
        DeliverResetRequest(reset);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        bool reset = false;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            Parent::WriteWord(addr, value);
            reset = TakeResetRequestLocked();
        }
        DeliverResetRequest(reset);
    }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lock(mtx_);
        w.Write("wcr", wcr_);
        w.Write("wsr", wsr_);
        w.Write("wicr", wicr_);
        w.Write("wrsr", wrsr_);
        w.Write("service_phase", service_phase_);
        w.Write("wcr_policy_locked", static_cast<uint8_t>(wcr_policy_locked_ ? 1u : 0u));
        w.Write("wicr_policy_locked", static_cast<uint8_t>(wicr_policy_locked_ ? 1u : 0u));
        const int64_t now = this->emu_.Get<VirtualClock>().NowNs();
        w.Write("restored_remaining_ns", timer_->RemainingNs(now));
        w.Write("restored_interrupt_remaining_ns", interrupt_timer_->RemainingNs(now));
    }
    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lock(mtx_);
        r.Read("wcr", wcr_);
        r.Read("wsr", wsr_);
        r.Read("wicr", wicr_);
        r.Read("wrsr", wrsr_);
        r.Read("service_phase", service_phase_);
        uint8_t policy_locked = 0u;
        r.Read("wcr_policy_locked", policy_locked);
        wcr_policy_locked_ = policy_locked != 0u;
        r.Read("wicr_policy_locked", policy_locked);
        wicr_policy_locked_ = policy_locked != 0u;
        r.Read("restored_remaining_ns", restored_remaining_ns_);
        r.Read("restored_interrupt_remaining_ns", restored_interrupt_remaining_ns_);
        reset_requested_ = false;
    }

    void PostRestore() override {
        std::lock_guard<std::mutex> lock(mtx_);
        const int64_t now = this->emu_.Get<VirtualClock>().NowNs();
        timer_->Arm(VirtualTimerList::DeadlineFromRemainingNs(now, restored_remaining_ns_));
        interrupt_timer_->Arm(VirtualTimerList::DeadlineFromRemainingNs(now, restored_interrupt_remaining_ns_));
        UpdateInterrupt();
    }

protected:
    uint16_t ReadReg16(uint32_t off) override {
        switch (off) {
        case kWcr: return wcr_;
        case kWsr: return wsr_;
        case kWrsr: return wrsr_;
        case kWicr: return wicr_;
        case kWmcr: return 0u;
        }
        this->HaltUnsupportedAccess("ReadReg16", Base + off, 0);
    }

    void WriteReg16(uint32_t off, uint16_t value) override {
        switch (off) {
        case kWcr: {
            LOG(SocWdt, "i.MX6 WDOG%u WCR 0x%04X -> 0x%04X at %lld ns\n",
                Base == 0x020BC000u ? 1u : 2u, wcr_, value,
                static_cast<long long>(this->emu_.Get<VirtualClock>().NowNs()));
            const bool was_enabled = (wcr_ & 0x0004u) != 0u;
            constexpr uint16_t kPolicyWriteOnce = 0x0083u; /* WDW, WDBG, WDZST */
            constexpr uint16_t kWriteOneOnce = 0x000Cu;    /* WDT, WDE */
            uint16_t next = value;
            if (wcr_policy_locked_)
                next = static_cast<uint16_t>((next & ~kPolicyWriteOnce) |
                                             (wcr_ & kPolicyWriteOnce));
            next = static_cast<uint16_t>(next | (wcr_ & kWriteOneOnce));
            /* IMX6DQRM Rev.2 §70.7.1: WDA[5] = 0 asserts WDOG_B, which Table 70-1
               says powers down the chip. */
            if ((value & 0x0020u) == 0u)
                this->HaltUnsupportedAccess("WCR WDA chip power-down", Base + off, value);
            /* IMX6DQRM Rev.2 §70.7.1: SRS "automatically resets to '1' after it has
               been asserted to '0'". */
            next = static_cast<uint16_t>(next | 0x0010u);
            wcr_ = next;
            wcr_policy_locked_ = true;
            if (!was_enabled && (wcr_ & 0x0004u) != 0u) ReloadCounter();
            if ((value & 0x0010u) == 0u) TriggerResetLocked(0x0001u);
            return;
        }
        case kWsr:
            LOG(SocWdt, "i.MX6 WDOG%u WSR <- 0x%04X phase=%u at %lld ns\n",
                Base == 0x020BC000u ? 1u : 2u, value, service_phase_,
                static_cast<long long>(this->emu_.Get<VirtualClock>().NowNs()));
            wsr_ = value;
            if (service_phase_ == 0u && value == 0x5555u) {
                service_phase_ = 1u;
            } else if (service_phase_ == 1u && value == 0xAAAAu) {
                service_phase_ = 0u;
                if ((wcr_ & 0x0004u) != 0u) ReloadCounter();
            } else {
                service_phase_ = 0u;
            }
            return;
        case kWicr: {
            LOG(SocWdt, "i.MX6 WDOG%u WICR 0x%04X -> 0x%04X at %lld ns\n",
                Base == 0x020BC000u ? 1u : 2u, wicr_, value,
                static_cast<long long>(this->emu_.Get<VirtualClock>().NowNs()));
            constexpr uint16_t kWie = 0x8000u;
            constexpr uint16_t kWtis = 0x4000u;
            constexpr uint16_t kWict = 0x00FFu;
            const uint16_t status = static_cast<uint16_t>(wicr_ & kWtis & ~value);
            const uint16_t policy = wicr_policy_locked_
                                        ? static_cast<uint16_t>(wicr_ & (kWie | kWict))
                                        : static_cast<uint16_t>(value & (kWie | kWict));
            wicr_ = static_cast<uint16_t>(policy | status);
            wicr_policy_locked_ = true;
            UpdateInterrupt();
            ArmPretimeoutInterrupt();
            return;
        }
        case kWmcr:
            /* IMX6DQRM Rev.2 §70.7.5: bits 15:1 are reserved and read as zero, and once PDE[0] has
               been cleared "this counter cannot be enabled again", so a later write cannot revive it.
               u-boot imx_wdog_disable_powerdown() clears it before the kernel on every i.MX6. */
            if ((value & ~1u) != 0u) this->HaltUnsupportedAccess("WriteReg16", Base + off, value);
            return;
        }
        this->HaltUnsupportedAccess("WriteReg16", Base + off, value);
    }

private:
    void ReloadCounter() {
        const int64_t half_seconds = static_cast<int64_t>((wcr_ >> 8u) + 1u);
        timer_->Arm(this->emu_.Get<VirtualClock>().NowNs() + half_seconds * 500000000ll);
        ArmPretimeoutInterrupt();
    }

    void ArmPretimeoutInterrupt() {
        constexpr uint16_t kWie = 0x8000u;
        constexpr uint16_t kWtis = 0x4000u;
        const int64_t timeout_deadline = timer_->DeadlineNs();
        if ((wicr_ & kWie) == 0u || (wicr_ & kWtis) != 0u ||
            timeout_deadline == VirtualTimerList::kNoDeadline) {
            interrupt_timer_->Arm(VirtualTimerList::kNoDeadline);
            return;
        }
        const int64_t lead_ns = static_cast<int64_t>(wicr_ & 0x00FFu) * 500000000ll;
        const int64_t now = this->emu_.Get<VirtualClock>().NowNs();
        interrupt_timer_->Arm((timeout_deadline - lead_ns) > now
                                  ? timeout_deadline - lead_ns
                                  : now);
    }

    void OnInterrupt() {
        std::lock_guard<std::mutex> lock(mtx_);
        if ((wicr_ & 0x8000u) == 0u) return;
        wicr_ |= 0x4000u;
        UpdateInterrupt();
    }

    void UpdateInterrupt() {
        auto& gic = this->emu_.Get<Imx6Gic>();
        constexpr int kSpi = Base == 0x020BC000u ? 80 : 81;
        if ((wicr_ & 0xC000u) == 0xC000u)
            gic.AssertSpi(kSpi);
        else
            gic.DeAssertSpi(kSpi);
    }

    void OnTimeout() {
        bool reset = false;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            LOG(SocWdt, "i.MX6 WDOG%u timeout WCR=0x%04X at %lld ns\n",
                Base == 0x020BC000u ? 1u : 2u, wcr_,
                static_cast<long long>(this->emu_.Get<VirtualClock>().NowNs()));
            if ((wcr_ & 0x0004u) != 0u) TriggerResetLocked(0x0002u);
            reset = TakeResetRequestLocked();
        }
        DeliverResetRequest(reset);
    }

    /* IMX6DQRM Rev.2 Table 70-1: "WDOG2_RESET_B_DEB - This signal is a reset source
       for the chip", and §70.4 activates wdog_rst_b for either module. */
    void TriggerResetLocked(uint16_t cause) {
        timer_->Arm(VirtualTimerList::kNoDeadline);
        interrupt_timer_->Arm(VirtualTimerList::kNoDeadline);
        wrsr_ = cause;
        if constexpr (Base == 0x020BC000u) {
            reset_requested_ = true;
        } else {
            this->emu_.template Get<Fatal>().Die(
                "i.MX6 WDOG2 requests a chip reset (cause 0x%04X), which is not modelled",
                cause);
        }
    }

    bool TakeResetRequestLocked() {
        const bool requested = reset_requested_;
        reset_requested_ = false;
        return requested;
    }

    void DeliverResetRequest(bool requested) {
        if (requested) this->emu_.Get<GuestCpuReset>().WatchdogReset();
    }

    void ResetRegisters(ResetKind kind) {
        std::lock_guard<std::mutex> lock(mtx_);
        /* IMX6DQRM Rev.2 §70.7.1: WCR.WDT clears only at POR. */
        const uint16_t retained_wdt = kind != ResetKind::Cold ? wcr_ & 0x0008u : 0u;
        wcr_ = static_cast<uint16_t>(kWcrReset | retained_wdt);
        /* IMX6DQRM Rev.2 §70.7.3: WRSR.POR reports power-on reset. */
        if (kind == ResetKind::Cold) wrsr_ = kWrsrPor;
        wsr_ = 0u;
        wicr_ = kWicrReset;
        service_phase_ = 0u;
        wcr_policy_locked_ = false;
        wicr_policy_locked_ = false;
        reset_requested_ = false;
        timer_->Arm(VirtualTimerList::kNoDeadline);
        interrupt_timer_->Arm(VirtualTimerList::kNoDeadline);
        UpdateInterrupt();
    }

    uint16_t wcr_ = kWcrReset;
    uint16_t wsr_ = 0;
    uint16_t wicr_ = kWicrReset;
    uint16_t wrsr_ = kWrsrPor;
    uint8_t service_phase_ = 0u;
    bool wcr_policy_locked_ = false;
    bool wicr_policy_locked_ = false;
    bool reset_requested_ = false;
    int64_t restored_remaining_ns_ = VirtualTimerList::kNoDeadline;
    int64_t restored_interrupt_remaining_ns_ = VirtualTimerList::kNoDeadline;
    VirtualTimerList::Entry* timer_ = nullptr;
    VirtualTimerList::Entry* interrupt_timer_ = nullptr;
    std::mutex mtx_;
};

}
