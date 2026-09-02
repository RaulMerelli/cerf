#pragma once

#include "../freescale_wdog_impl.h"

#include "../../core/cerf_emulator.h"
#include "../../core/virtual_clock.h"
#include "../../core/virtual_timer_list.h"
#include "../../host/guest_deep_sleep.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"
#include "imx6_gic.h"

#include <mutex>

/* i.MX6 WDOG1/WDOG2 share one register set; only the base differs
   (IMX6SDLRM Rev.4 section 70.7 memory map: WDOG1 at 20B_C000, WDOG2 at
   20C_0000, reset values WCR 0030h, WSR 0000h, WICR 0004h, WMCR 0001h). */
namespace cerf_imx6_wdog_detail {

using cerf_freescale_wdog_detail::FreescaleWdogBase;
using cerf_freescale_wdog_detail::kWcr;
using cerf_freescale_wdog_detail::kWcrReset;
using cerf_freescale_wdog_detail::kWicr;
using cerf_freescale_wdog_detail::kWmcr;
using cerf_freescale_wdog_detail::kWrsr;
using cerf_freescale_wdog_detail::kWsr;

constexpr uint16_t kWicrReset = 0x0004u;
constexpr uint16_t kWmcrReset = 0x0001u;

/* IMX6SDLRM Rev.4 section 70.7.3: WRSR records the source of the last reset
   the WDOG produced, "only one bit in the WRSR will always be asserted high",
   with POR at bit 4, TOUT at bit 1 and SFTW at bit 0.  CERF starts the guest
   from a power-on reset, so POR is the asserted bit. */
constexpr uint16_t kWrsrPor = 0x0010u;

template <uint32_t Base> class Imx6WdogBase : public FreescaleWdogBase<Base, SocFamily::iMX6> {
public:
    using Parent = FreescaleWdogBase<Base, SocFamily::iMX6>;
    using Parent::Parent;

    void OnReady() override {
        Parent::OnReady();
        interrupt_timer_ = this->emu_.Get<VirtualTimerList>().Add([this] { OnInterrupt(); });
        timer_ = this->emu_.Get<VirtualTimerList>().Add([this] { OnTimeout(); });
        powerdown_timer_ = this->emu_.Get<VirtualTimerList>().Add([this] { OnPowerDown(); });
        {
            std::lock_guard<std::mutex> lock(mtx_);
            ArmPowerDownCounter();
        }
        this->emu_.Get<GuestCpuReset>().RegisterResetListener(
            [this](ResetLineKind kind) { ResetRegisters(kind); });
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
        w.Write(wcr_);
        w.Write(wsr_);
        w.Write(wicr_);
        w.Write(wmcr_);
        w.Write(wrsr_);
        w.Write(service_phase_);
        w.Write(static_cast<uint8_t>(wcr_policy_locked_ ? 1u : 0u));
        w.Write(static_cast<uint8_t>(wicr_policy_locked_ ? 1u : 0u));
        w.Write(timer_->DeadlineNs());
        w.Write(interrupt_timer_->DeadlineNs());
        w.Write(powerdown_timer_->DeadlineNs());
    }
    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lock(mtx_);
        r.Read(wcr_);
        r.Read(wsr_);
        r.Read(wicr_);
        r.Read(wmcr_);
        r.Read(wrsr_);
        r.Read(service_phase_);
        uint8_t policy_locked = 0u;
        r.Read(policy_locked);
        wcr_policy_locked_ = policy_locked != 0u;
        r.Read(policy_locked);
        wicr_policy_locked_ = policy_locked != 0u;
        r.Read(restored_deadline_ns_);
        r.Read(restored_interrupt_deadline_ns_);
        r.Read(restored_powerdown_deadline_ns_);
        reset_requested_ = false;
    }

    void PostRestore() override {
        std::lock_guard<std::mutex> lock(mtx_);
        timer_->Arm(restored_deadline_ns_);
        interrupt_timer_->Arm(restored_interrupt_deadline_ns_);
        powerdown_timer_->Arm(restored_powerdown_deadline_ns_);
        UpdateInterrupt();
    }

protected:
    uint16_t ReadReg16(uint32_t off) override {
        switch (off) {
        case kWcr: return wcr_;
        case kWsr: return wsr_;
        case kWrsr: return wrsr_;
        case kWicr: return wicr_;
        case kWmcr: return wmcr_;
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
            LOG(SocWdt, "i.MX6 WDOG%u WMCR 0x%04X -> 0x%04X at %lld ns\n",
                Base == 0x020BC000u ? 1u : 2u, wmcr_, value,
                static_cast<long long>(this->emu_.Get<VirtualClock>().NowNs()));
            /* PDE resets enabled and is one-way: software may disable the
               16-second counter, but cannot enable it again before reset. */
            if ((wmcr_ & 1u) != 0u && (value & 1u) == 0u) {
                wmcr_ = 0u;
                powerdown_timer_->Arm(VirtualTimerList::kNoDeadline);
            }
            return;
        }
        /* Section 70.7.3: "Any write performed on this register will generate a
           Peripheral Bus Error", which CERF does not model, so a WRSR write
           halts here rather than being accepted silently. */
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
        auto* gic = this->emu_.TryGet<Imx6Gic>();
        if (!gic) return;
        constexpr int kSpi = Base == 0x020BC000u ? 80 : 81;
        if ((wicr_ & 0xC000u) == 0xC000u)
            gic->AssertSpi(kSpi);
        else
            gic->DeAssertSpi(kSpi);
    }

    void ArmPowerDownCounter() {
        if ((wmcr_ & 1u) != 0u)
            powerdown_timer_->Arm(this->emu_.Get<VirtualClock>().NowNs() + 16000000000ll);
        else
            powerdown_timer_->Arm(VirtualTimerList::kNoDeadline);
    }

    void OnPowerDown() {
        bool power_down = false;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            power_down = (wmcr_ & 1u) != 0u;
        }
        if (power_down) this->emu_.Get<GuestDeepSleep>().EnterPowerOff();
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

    void TriggerResetLocked(uint16_t cause) {
        timer_->Arm(VirtualTimerList::kNoDeadline);
        interrupt_timer_->Arm(VirtualTimerList::kNoDeadline);
        wrsr_ = cause;
        if constexpr (Base == 0x020BC000u) reset_requested_ = true;
    }

    bool TakeResetRequestLocked() {
        const bool requested = reset_requested_;
        reset_requested_ = false;
        return requested;
    }

    void DeliverResetRequest(bool requested) {
        if (requested) this->emu_.Get<GuestCpuReset>().WatchdogReset();
    }

    void ResetRegisters(ResetLineKind kind) {
        std::lock_guard<std::mutex> lock(mtx_);
        const uint16_t retained_wdt = kind == ResetLineKind::Other ? wcr_ & 0x0008u : 0u;
        wcr_ = static_cast<uint16_t>(kWcrReset | retained_wdt);
        wsr_ = 0u;
        wicr_ = kWicrReset;
        wmcr_ = kWmcrReset;
        service_phase_ = 0u;
        wcr_policy_locked_ = false;
        wicr_policy_locked_ = false;
        reset_requested_ = false;
        timer_->Arm(VirtualTimerList::kNoDeadline);
        interrupt_timer_->Arm(VirtualTimerList::kNoDeadline);
        UpdateInterrupt();
        ArmPowerDownCounter();
    }

    uint16_t wcr_ = kWcrReset;
    uint16_t wsr_ = 0;
    uint16_t wicr_ = kWicrReset;
    uint16_t wmcr_ = kWmcrReset;
    uint16_t wrsr_ = kWrsrPor;
    uint8_t service_phase_ = 0u;
    bool wcr_policy_locked_ = false;
    bool wicr_policy_locked_ = false;
    bool reset_requested_ = false;
    int64_t restored_deadline_ns_ = VirtualTimerList::kNoDeadline;
    int64_t restored_interrupt_deadline_ns_ = VirtualTimerList::kNoDeadline;
    int64_t restored_powerdown_deadline_ns_ = VirtualTimerList::kNoDeadline;
    VirtualTimerList::Entry* timer_ = nullptr;
    VirtualTimerList::Entry* interrupt_timer_ = nullptr;
    VirtualTimerList::Entry* powerdown_timer_ = nullptr;
    std::mutex mtx_;
};

} /* namespace cerf_imx6_wdog_detail */
