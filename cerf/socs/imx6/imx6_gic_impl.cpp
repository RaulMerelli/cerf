#include "imx6_gic.h"
#include "../../cpu/arm_processor_config.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../core/log.h"
#include "../../jit/arm/arm_jit.h"
#include "../../jit/arm/arm_mmu.h"
#include "../../jit/arm/cpu_state.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"
#include "imx6_gic_aux.h"

#include <algorithm>
#include <atomic>
#include <mutex>
namespace {

using imx6_gic_detail::Imx6GicAux;

class Imx6GicImpl final : public ::Imx6Gic {
public:
    using ::Imx6Gic::Imx6Gic;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void AssertSpi(int spi) override {
        const int gic_id = spi + 32;
        const int w = gic_id >> 5;
        const uint32_t bit = 1u << (gic_id & 31);
        bool deliver = false;
        {
            std::lock_guard<std::mutex> lk(timer_mutex_);
            if (w < 5) {
                const bool was_high = (line_level_[w] & bit) != 0u;
                line_level_[w] |= bit;
                if (!IsEdgeTriggeredLocked(gic_id) || !was_high) pending_[w] |= bit;
            }
            deliver = DeliverPendingLocked();
        }
        if (deliver) emu_.Get<ArmJit>().SetInterruptPending();
    }
    void DeAssertSpi(int spi) override {
        const int gic_id = spi + 32;
        const int w = gic_id >> 5;
        const uint32_t bit = 1u << (gic_id & 31);
        std::lock_guard<std::mutex> lk(timer_mutex_);
        if (w < 5) {
            line_level_[w] &= ~bit;
            if (!IsEdgeTriggeredLocked(gic_id)) pending_[w] &= ~bit;
        }
    }

    uint32_t ReadMmio(uint32_t off) override {
        switch (off) {
        case 0x100: return cpu_control_;
        case 0x104: return priority_mask_;

        case 0x10C: {
            std::lock_guard<std::mutex> lk(timer_mutex_);
            const uint32_t now = GuestCycles();
            AdvancePrivateTimerLocked(now);
            aux_.AdvanceGlobalTimer(now, kPeriphDiv);
            if (active_irq_ != 1023u) return 1023u;
            RefreshLevelPendingLocked();
            const uint32_t id = HighestPriorityPendingLocked();
            if (id == 1023u) return 1023u;
            active_irq_ = id;
            if (id >= 32u) pending_[id >> 5] &= ~(1u << (id & 31));
            emu_.Get<ArmJit>().ClearInterruptPending();
            return id;
        }
        /* QEMU i.MX6/A9MPCore GIC model. */
        case 0x1FC: return 0x0001043Bu;
        case 0x200:
        case 0x204: {
            std::lock_guard<std::mutex> lk(timer_mutex_);
            aux_.AdvanceGlobalTimer(GuestCycles(), kPeriphDiv);
            uint32_t value = 0u;
            aux_.ReadMmio(off, value);
            return value;
        }
        case 0x600: return private_timer_load_;
        case 0x604: {
            const uint32_t counter = ComputePrivateCounterFast(GuestCycles());
            return counter;
        }
        case 0x608: return private_timer_control_;
        case 0x60C: {
            std::lock_guard<std::mutex> lk(timer_mutex_);
            AdvancePrivateTimerLocked(GuestCycles());
            return private_timer_status_;
        }
        case 0x1000: return distributor_control_;
        case 0x1004: return 4u;
        case 0x1008: return 0x0000043Bu;
        default: break;
        }

        if (off >= 0x1100u && off < 0x1114u) return enabled_[(off - 0x1100u) >> 2];
        if (off >= 0x1180u && off < 0x1194u) return enabled_[(off - 0x1180u) >> 2];
        if (off >= 0x1200u && off < 0x1214u) return pending_[(off - 0x1200u) >> 2];
        if (off >= 0x1280u && off < 0x1294u) return pending_[(off - 0x1280u) >> 2];
        if (off >= 0x1400u && off < 0x14A0u) return priorities_[(off - 0x1400u) >> 2];
        /* ARM DDI 0407F §3.3.3: for systems that support only one Cortex-A9 processor the
           ICDIPTR registers read as zero and writes are ignored; ICDICTR above reports one. */
        if (off >= 0x1800u && off < 0x18A0u) return 0u;
        if (off >= 0x1C00u && off < 0x1C28u) return configuration_[(off - 0x1C00u) >> 2];
        {
            uint32_t value = 0u;
            if (aux_.ScuRead(off, emu_.Get<ArmMmu>().State()->aux_control_register, value)) return value;
            if (aux_.ReadMmio(off, value)) return value;
        }
        emu_.Get<Fatal>().Die("[GIC] unsupported read32 at 0x%08X", kMmioBase + off);
    }

    void WriteMmio(uint32_t off, uint32_t value) override {
        switch (off) {
        case 0x100: cpu_control_ = value; return;
        case 0x104: priority_mask_ = value & 0xFFu; return;

        case 0x110: {
            bool deliver = false;
            {
                std::lock_guard<std::mutex> lk(timer_mutex_);
                if ((value & 0x3FFu) == active_irq_) active_irq_ = 1023u;
                RefreshLevelPendingLocked();
                aux_.AdvanceGlobalTimer(GuestCycles(), kPeriphDiv);
                deliver = DeliverPendingLocked();
            }
            if (deliver) emu_.Get<ArmJit>().SetInterruptPending();
            return;
        }
        case 0x200:
        case 0x204: {
            std::lock_guard<std::mutex> lk(timer_mutex_);
            aux_.WriteMmio(off, value);
            aux_.ResetGlobalTimerAnchor(GuestCycles());
            return;
        }
        case 0x208: {
            std::lock_guard<std::mutex> lk(timer_mutex_);
            aux_.AdvanceGlobalTimer(GuestCycles(), kPeriphDiv);
            aux_.WriteMmio(off, value);
            aux_.ResetGlobalTimerAnchor(GuestCycles());
            return;
        }
        case 0x600: {
            std::lock_guard<std::mutex> lk(timer_mutex_);
            private_timer_load_ = value;
            private_timer_counter_ = value;
            pt_anchor_cycles_ = GuestCycles();
            PublishPrivateTimerFastLocked();
            return;
        }
        case 0x604: {
            std::lock_guard<std::mutex> lk(timer_mutex_);
            private_timer_counter_ = value;
            pt_anchor_cycles_ = GuestCycles();
            PublishPrivateTimerFastLocked();
            return;
        }
        case 0x608: {
            std::lock_guard<std::mutex> lk(timer_mutex_);
            const uint32_t now = GuestCycles();
            AdvancePrivateTimerLocked(now);
            private_timer_counter_ = ComputePrivateCounterLocked(now);
            private_timer_control_ = value;
            pt_anchor_cycles_ = now;
            if ((value & 1u) != 0 && private_timer_counter_ == 0) private_timer_counter_ = private_timer_load_;
            PublishPrivateTimerFastLocked();
            return;
        }
        case 0x60C: {
            std::lock_guard<std::mutex> lk(timer_mutex_);
            AdvancePrivateTimerLocked(GuestCycles());
            private_timer_status_ &= ~value;
            PublishPrivateTimerFastLocked();
            return;
        }
        case 0x1000: distributor_control_ = value & 3u; return;
        default: break;
        }
        /* ARM IHI 0048B §3.9.2: every interrupt belongs to Group 0 at reset, and an assignment to
           Group 1 routes it away from the Group 0 exception this model delivers. */
        if (off >= 0x1080u && off < 0x1094u) {
            if (value != 0u)
                emu_.Get<Fatal>().Die("[GIC] interrupt grouping is not modelled (0x%08X at 0x%08X)", value, kMmioBase + off);
            return;
        }
        if (off >= 0x1100u && off < 0x1114u) {
            bool deliver = false;
            {
                std::lock_guard<std::mutex> lk(timer_mutex_);
                enabled_[(off - 0x1100u) >> 2] |= value;
                RefreshLevelPendingLocked();
                aux_.AdvanceGlobalTimer(GuestCycles(), kPeriphDiv);
                deliver = DeliverPendingLocked();
            }
            if (deliver) emu_.Get<ArmJit>().SetInterruptPending();
            return;
        }
        if (off >= 0x1180u && off < 0x1194u) {
            std::lock_guard<std::mutex> lk(timer_mutex_);
            enabled_[(off - 0x1180u) >> 2] &= ~value;
            return;
        }
        if (off >= 0x1200u && off < 0x1214u) {
            pending_[(off - 0x1200u) >> 2] |= value;
            return;
        }
        if (off >= 0x1280u && off < 0x1294u) {
            std::lock_guard<std::mutex> lk(timer_mutex_);
            pending_[(off - 0x1280u) >> 2] &= ~value;
            RefreshLevelPendingLocked();
            return;
        }
        if (off >= 0x1400u && off < 0x14A0u) {
            priorities_[(off - 0x1400u) >> 2] = value;
            return;
        }
        if (off >= 0x1800u && off < 0x18A0u) return;
        if (off >= 0x1C00u && off < 0x1C28u) {
            std::lock_guard<std::mutex> lk(timer_mutex_);
            configuration_[(off - 0x1C00u) >> 2] = value;
            RefreshLevelPendingLocked();
            return;
        }
        {
            const char* unmodelled = nullptr;
            if (aux_.ScuWrite(off, value, unmodelled)) {
                if (unmodelled != nullptr)
                    emu_.Get<Fatal>().Die("[GIC] %s is not modelled (0x%08X)", unmodelled, value);
                return;
            }
        }
        if (aux_.WriteMmio(off, value)) return;
        emu_.Get<Fatal>().Die("[GIC] unsupported write32 at 0x%08X val=0x%08X", kMmioBase + off, value);
    }

    void SaveGicState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(timer_mutex_);
        w.Write(pt_anchor_cycles_);
        w.Write(active_irq_);
        w.Write(cpu_control_);
        w.Write(priority_mask_);
        w.Write(private_timer_load_);
        w.Write(private_timer_counter_);
        w.Write(private_timer_control_);
        w.Write(private_timer_status_);
        w.Write(distributor_control_);
        aux_.SaveState(w);
        w.WriteBytes(enabled_, sizeof(enabled_));
        w.WriteBytes(pending_, sizeof(pending_));
        w.WriteBytes(line_level_, sizeof(line_level_));
        w.WriteBytes(priorities_, sizeof(priorities_));
        w.WriteBytes(configuration_, sizeof(configuration_));
    }
    void RestoreGicState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(timer_mutex_);
        r.Read(pt_anchor_cycles_);
        r.Read(active_irq_);
        r.Read(cpu_control_);
        r.Read(priority_mask_);
        r.Read(private_timer_load_);
        r.Read(private_timer_counter_);
        r.Read(private_timer_control_);
        r.Read(private_timer_status_);
        r.Read(distributor_control_);
        aux_.RestoreState(r);
        r.ReadBytes(enabled_, sizeof(enabled_));
        r.ReadBytes(pending_, sizeof(pending_));
        r.ReadBytes(line_level_, sizeof(line_level_));
        r.ReadBytes(priorities_, sizeof(priorities_));
        r.ReadBytes(configuration_, sizeof(configuration_));
        PublishPrivateTimerFastLocked();
    }
    void PostRestoreGicState() override {
        std::lock_guard<std::mutex> lk(timer_mutex_);
        RefreshLevelPendingLocked();
        if (active_irq_ != 1023u || HighestPriorityPendingLocked() != 1023u)
            emu_.Get<ArmJit>().SetInterruptPending();
    }

    void OnReady() override {
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            {
                std::lock_guard<std::mutex> lk(timer_mutex_);
                ResetLocked();
            }
            emu_.Get<ArmJit>().ClearInterruptPending();
        });
    }

private:
    void ResetLocked() {
        pt_anchor_cycles_ = 0;
        active_irq_ = 1023u;
        cpu_control_ = 0;
        priority_mask_ = 0;
        private_timer_load_ = 0;
        private_timer_counter_ = 0;
        private_timer_control_ = 0;
        private_timer_status_ = 0;
        distributor_control_ = 0;
        std::fill(std::begin(enabled_), std::end(enabled_), 0u);
        std::fill(std::begin(pending_), std::end(pending_), 0u);
        std::fill(std::begin(line_level_), std::end(line_level_), 0u);
        std::fill(std::begin(priorities_), std::end(priorities_), 0u);
        std::fill(std::begin(configuration_), std::end(configuration_), 0u);
        aux_.Reset();
        PublishPrivateTimerFastLocked();
    }

    static constexpr uint32_t kPeriphDiv = 2u;
    static constexpr int64_t kMinSampleNs = 1000;       /* one microsecond */
    static constexpr int64_t kIdleSampleNs = 1000000;   /* one millisecond, timer disabled */
    uint32_t GuestCycles() const { return emu_.Get<ArmJit>().CpuState()->guest_cycle_counter; }

    void AdvancePrivateTimerLocked(uint32_t cycles_now) {
        if ((private_timer_control_ & 1u) == 0) {
            pt_anchor_cycles_ = cycles_now;
            PublishPrivateTimerFastLocked();
            return;
        }
        const uint32_t presc = ((private_timer_control_ >> 8) & 0xFFu) + 1u;
        const uint32_t start = private_timer_counter_ ? private_timer_counter_ : private_timer_load_;
        const uint64_t period = static_cast<uint64_t>(start + 1u) * presc * kPeriphDiv;
        if (period == 0) return;
        const uint32_t elapsed = cycles_now - pt_anchor_cycles_;
        if (elapsed >= period) {
            private_timer_status_ |= 1u;
            if ((private_timer_control_ & 2u) != 0) {
                const uint64_t reload_period = static_cast<uint64_t>(private_timer_load_ + 1u) * presc * kPeriphDiv;
                const uint64_t fires = reload_period ? elapsed / reload_period : 1u;
                pt_anchor_cycles_ += static_cast<uint32_t>(fires * reload_period);
                private_timer_counter_ = private_timer_load_;
            } else {
                private_timer_counter_ = 0;
                pt_anchor_cycles_ = cycles_now;
            }
            PublishPrivateTimerFastLocked();
        }
    }

    uint32_t ComputePrivateCounterLocked(uint32_t cycles_now) const {
        if ((private_timer_control_ & 1u) == 0) return private_timer_counter_;
        const uint32_t presc = ((private_timer_control_ >> 8) & 0xFFu) + 1u;
        const uint64_t cyc_per_tick = static_cast<uint64_t>(presc) * kPeriphDiv;
        if (cyc_per_tick == 0) return private_timer_counter_;
        const uint32_t start = private_timer_counter_ ? private_timer_counter_ : private_timer_load_;
        const uint32_t pos = static_cast<uint32_t>((cycles_now - pt_anchor_cycles_) / cyc_per_tick);
        return (pos > start) ? 0u : (start - pos);
    }

    uint32_t ComputePrivateCounterFast(uint32_t cycles_now) const {
        const uint32_t control = private_timer_control_fast_.load(std::memory_order_relaxed);
        const uint32_t counter = private_timer_counter_fast_.load(std::memory_order_relaxed);
        if ((control & 1u) == 0) return counter;
        const uint32_t presc = ((control >> 8) & 0xFFu) + 1u;
        const uint64_t cyc_per_tick = static_cast<uint64_t>(presc) * kPeriphDiv;
        const uint32_t start = counter ? counter : private_timer_load_fast_.load(std::memory_order_relaxed);
        const uint32_t pos =
            static_cast<uint32_t>((cycles_now - pt_anchor_cycles_fast_.load(std::memory_order_relaxed)) / cyc_per_tick);
        return (pos > start) ? 0u : (start - pos);
    }

    void PublishPrivateTimerFastLocked() {
        private_timer_load_fast_.store(private_timer_load_, std::memory_order_relaxed);
        private_timer_counter_fast_.store(private_timer_counter_, std::memory_order_relaxed);
        private_timer_control_fast_.store(private_timer_control_, std::memory_order_relaxed);
        pt_anchor_cycles_fast_.store(pt_anchor_cycles_, std::memory_order_relaxed);
    }

    bool GlobalTimerIrqPendingLocked() const {
        if (!aux_.GlobalTimerIrqPending()) return false;
        if ((distributor_control_ & 1u) == 0) return false;
        if ((cpu_control_ & 1u) == 0) return false;
        return (enabled_[0] & (1u << 27)) != 0;
    }

    bool PrivateTimerIrqPendingLocked() const {
        if ((private_timer_control_ & 4u) == 0) return false;
        if ((private_timer_status_ & 1u) == 0) return false;
        if ((distributor_control_ & 1u) == 0) return false;
        if ((cpu_control_ & 1u) == 0) return false;
        return (enabled_[0] & (1u << 29)) != 0;
    }

    bool IsEdgeTriggeredLocked(int gic_id) const {
        if (gic_id < 32 || gic_id >= 160) return false;
        const uint32_t cfg = configuration_[gic_id >> 4];
        const uint32_t hi_bit = 1u << (((gic_id & 15) * 2u) + 1u);
        return (cfg & hi_bit) != 0u;
    }

    void RefreshLevelPendingLocked() {
        for (int gic_id = 32; gic_id < 160; ++gic_id) {
            const int w = gic_id >> 5;
            const uint32_t bit = 1u << (gic_id & 31);
            if ((line_level_[w] & bit) != 0u && !IsEdgeTriggeredLocked(gic_id)) pending_[w] |= bit;
        }
    }

    bool PendingEnabledLocked(uint32_t id) const {
        if (id == 27u) return GlobalTimerIrqPendingLocked();
        if (id == 29u) return PrivateTimerIrqPendingLocked();
        if (id < 32u) return false;
        if ((distributor_control_ & cpu_control_ & 1u) == 0) return false;
        return (pending_[id >> 5] & enabled_[id >> 5] & (1u << (id & 31))) != 0u;
    }

    uint32_t PriorityOfLocked(uint32_t id) const {
        return (priorities_[id >> 2] >> ((id & 3u) * 8u)) & 0xFFu;
    }

    /* ARM IHI 0048B §3.7.2 HighestPriorityPendingInterrupt() replaces the candidate only on a
       strictly higher priority, and PriorityIsHigher(pr1, pr2) is UInt(pr1) < UInt(pr2); §3.3.2
       signals an interrupt only while its priority is higher than GICC_PMR. */
    uint32_t HighestPriorityPendingLocked() const {
        uint32_t best = 1023u;
        uint32_t best_priority = 0x100u;
        for (uint32_t id = 0; id < 160u; ++id) {
            if (!PendingEnabledLocked(id)) continue;
            const uint32_t priority = PriorityOfLocked(id);
            if (priority < best_priority) {
                best_priority = priority;
                best = id;
            }
        }
        if (best_priority >= priority_mask_) return 1023u;
        return best;
    }

    bool DeliverPendingLocked() const {
        return active_irq_ == 1023u && HighestPriorityPendingLocked() != 1023u;
    }

    /* ARM DDI 0407F §4.1: the private timer counts down at PERIPHCLK/(prescaler+1), and this
       model anchors it to the guest cycle counter, which arm_interrupt_channel.cpp advances at
       ArmProcessorConfig::CpuClockHz(). The sampler therefore waits the time the timer itself
       still needs, so its resolution follows the period the guest programmed. */
    int64_t NextSampleNs() override {
        std::lock_guard<std::mutex> lk(timer_mutex_);
        if ((private_timer_control_ & 1u) == 0u) return kIdleSampleNs;
        const uint32_t presc = ((private_timer_control_ >> 8) & 0xFFu) + 1u;
        const uint64_t cycles_per_tick = static_cast<uint64_t>(presc) * kPeriphDiv;
        const uint64_t ticks_left = ComputePrivateCounterLocked(GuestCycles()) + 1u;
        const uint64_t cpu_hz = emu_.Get<ArmProcessorConfig>().CpuClockHz();
        if (cpu_hz == 0u) return kIdleSampleNs;
        const int64_t ns = static_cast<int64_t>((ticks_left * cycles_per_tick * 1000000000ull) / cpu_hz);
        return ns < kMinSampleNs ? kMinSampleNs : (ns > kIdleSampleNs ? kIdleSampleNs : ns);
    }

    bool Tick() override {
        bool deliver = false;
        {
            std::lock_guard<std::mutex> lk(timer_mutex_);
            const uint32_t now = GuestCycles();
            AdvancePrivateTimerLocked(now);
            aux_.AdvanceGlobalTimer(now, kPeriphDiv);
            RefreshLevelPendingLocked();
            deliver = DeliverPendingLocked();
        }
        return deliver;
    }

    std::mutex timer_mutex_;
    Imx6GicAux aux_;
    uint32_t pt_anchor_cycles_ = 0;
    uint32_t active_irq_ = 1023u;
    uint32_t cpu_control_ = 0;
    uint32_t priority_mask_ = 0;
    uint32_t private_timer_load_ = 0;
    uint32_t private_timer_counter_ = 0;
    uint32_t private_timer_control_ = 0;
    uint32_t private_timer_status_ = 0;
    std::atomic<uint32_t> private_timer_load_fast_{0};
    std::atomic<uint32_t> private_timer_counter_fast_{0};
    std::atomic<uint32_t> private_timer_control_fast_{0};
    std::atomic<uint32_t> pt_anchor_cycles_fast_{0};
    uint32_t distributor_control_ = 0;
    uint32_t enabled_[5]{};
    uint32_t pending_[5]{};
    uint32_t line_level_[5]{};
    uint32_t priorities_[40]{};
    uint32_t configuration_[10]{};
};

}

REGISTER_SERVICE_AS(Imx6GicImpl, ::Imx6Gic);
