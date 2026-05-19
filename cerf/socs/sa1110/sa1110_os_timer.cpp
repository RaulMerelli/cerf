#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../core/rate_probe.h"
#include "../../boards/board_detector.h"
#include "../../cpu/arm_processor_config.h"
#include "../../jit/arm_cpu.h"
#include "../../jit/arm_jit.h"
#include "../../jit/cpu_state.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "sa1110_intc.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <intrin.h>
#include <mutex>
#include <thread>

namespace {

/* SA-1110 §9.2.1.1 Table 9-1: IP26..29 = OSMR0..3 match. */
constexpr uint32_t kIntcOst0Bit  = 26u;

constexpr auto kPollInterval = std::chrono::microseconds(100);

class Sa1110OsTimer : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardDetector>().GetSoc() == SocFamily::SA1110;
    }

    void OnReady() override {
        divider_ = emu_.Get<ArmProcessorConfig>().CpuToOscrDivider();
        if (divider_ == 0) divider_ = 1;
        baseline_packed_.store(PackBaseline(0, GuestCycles()),
                               std::memory_order_release);
        auto& pd = emu_.Get<PeripheralDispatcher>();
        pd.Register(this);
        pd.RegisterFastPath(
            MmioBase(), MmioBase() + 0x20u,
            &Sa1110OsTimer::FastReadThunk,
            &Sa1110OsTimer::FastWriteThunk,
            this);
        match_thread_ = std::thread([this] { MatchLoop(); });
    }

    static uint32_t FastReadThunk(void* ctx, uint32_t off, uint32_t width) {
        return static_cast<Sa1110OsTimer*>(ctx)->FastRead(off, width);
    }
    static void FastWriteThunk(void* ctx, uint32_t off, uint32_t value, uint32_t width) {
        static_cast<Sa1110OsTimer*>(ctx)->FastWrite(off, value, width);
    }

    uint32_t FastRead(uint32_t off, uint32_t width) {
        const uint32_t base  = off & ~0x3u;
        const uint32_t shift = (off & 0x3u) * 8;
        if (!IsKnown(base)) HaltUnsupportedAccess("FastRead", MmioBase() + off, 0);
        const uint32_t word = ReadReg(base);
        if (width == 4) return word;
        if (width == 2) return (word >> shift) & 0xFFFFu;
        return (word >> shift) & 0xFFu;
    }

    void FastWrite(uint32_t off, uint32_t value, uint32_t width) {
        const uint32_t base  = off & ~0x3u;
        const uint32_t shift = (off & 0x3u) * 8;
        if (!IsKnown(base)) HaltUnsupportedAccess("FastWrite", MmioBase() + off, value);
        if (width == 4) {
            WriteReg(base, value);
        } else {
            const uint32_t mask = (width == 2) ? 0xFFFFu : 0xFFu;
            const uint32_t cur = ReadReg(base);
            WriteReg(base, (cur & ~(mask << shift)) | ((value & mask) << shift));
        }
    }

    ~Sa1110OsTimer() override {
        stop_.store(true, std::memory_order_release);
        cv_.notify_all();
        if (match_thread_.joinable()) match_thread_.join();
    }

    /* SA-1110 §9.4.7 Table 9-1: OS Timer block PA base. */
    uint32_t MmioBase() const override { return 0x90000000u; }
    uint32_t MmioSize() const override { return 0x00010000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

private:
    std::mutex              cv_mtx_;
    std::condition_variable cv_;
    std::thread             match_thread_;
    std::atomic<bool>       stop_{false};

    /* High 32 = baseline_oscr, low 32 = baseline_cycles. Single 64-bit
       atomic so reader sees a consistent (oscr, cycles) pair — splitting
       into two 32-bit atomics produces torn reads when match thread
       re-baselines while JIT reads. */
    std::atomic<uint64_t> baseline_packed_{0};
    uint32_t              divider_{1};

    static uint64_t PackBaseline(uint32_t oscr, uint32_t cycles) {
        return (static_cast<uint64_t>(oscr) << 32) | cycles;
    }
    static uint32_t UnpackBaseOscr  (uint64_t p) { return static_cast<uint32_t>(p >> 32); }
    static uint32_t UnpackBaseCycles(uint64_t p) { return static_cast<uint32_t>(p); }

    /* High 32 bits = OSMR[n], low 32 bits = oscr_at_arm[n] snapshot. */
    std::atomic<uint64_t> osmr_arm_[4]{};
    std::atomic<uint32_t> ossr_{0};
    std::atomic<uint32_t> ower_{0};
    std::atomic<uint32_t> oier_{0};

    uint32_t GuestCycles() const {
        return emu_.Get<ArmJit>().CpuState()->guest_cycle_counter;
    }

    static uint64_t PackOsmr(uint32_t osmr, uint32_t arm) {
        return (static_cast<uint64_t>(osmr) << 32) | static_cast<uint64_t>(arm);
    }
    static uint32_t UnpackOsmr(uint64_t p) { return static_cast<uint32_t>(p >> 32); }
    static uint32_t UnpackArm (uint64_t p) { return static_cast<uint32_t>(p & 0xFFFFFFFFu); }

    uint32_t ReadOscr() const {
#if CERF_DEV_MODE
        emu_.Get<RateProbe>().Inc(RateProbe::Counter::OstReadOscr);
#endif
        const uint64_t packed = baseline_packed_.load(std::memory_order_acquire);
        const uint32_t cycles_now = GuestCycles();
        const uint32_t delta = cycles_now - UnpackBaseCycles(packed);
        return UnpackBaseOscr(packed) + (delta / divider_);
    }

    void WriteOscr(uint32_t value) {
        baseline_packed_.store(PackBaseline(value, GuestCycles()),
                               std::memory_order_release);
        cv_.notify_all();
    }

    /* MUST be called from match loop — without it 32-bit
       guest_cycle_counter wraps every ~43 s, ReadOscr's (cycles_now -
       base_cycles) unsigned-wraps, OSCR jumps backward, OEMIH catch-up
       hangs in 1.165M-iter 2^32 wrap. */
    void RebaseToCurrent() {
        const uint64_t old_packed = baseline_packed_.load(std::memory_order_acquire);
        const uint32_t cycles_now = GuestCycles();
        const uint32_t delta = cycles_now - UnpackBaseCycles(old_packed);
        const uint32_t new_oscr = UnpackBaseOscr(old_packed) + (delta / divider_);
        baseline_packed_.store(PackBaseline(new_oscr, cycles_now),
                               std::memory_order_release);
    }

    /* §9.4.4: M[N] sets on rising clock edge when OSCR equals OSMR[N];
       oscr_at_arm anchors the forward-crossing test that detects
       exactly that edge across wallclock-jumping OSCR samples. */
    static bool MatchHasFired(uint32_t oscr_at_arm,
                              uint32_t osmr,
                              uint32_t oscr_now) {
        const uint32_t forward_to_target = osmr     - oscr_at_arm;
        const uint32_t forward_to_now    = oscr_now - oscr_at_arm;
        return forward_to_now >= forward_to_target;
    }

    /* PocketPC clockevents probes OSMR0 in a tight loop with growing
       delta; only the imminent-match writes need to wake the worker. */
    static constexpr uint32_t kNotifyForwardLimit = 10000u;

    void WriteOsmr(int n, uint32_t value) {
        const uint32_t oscr_now = ReadOscr();
        osmr_arm_[n].store(PackOsmr(value, oscr_now),
                           std::memory_order_release);

        const uint32_t forward = value - oscr_now;
        if (forward < kNotifyForwardLimit) {
            cv_.notify_all();
        }
    }

    void WriteOssr(uint32_t value) {
        /* §9.4.4: writing 1 to OSSR.M[N] clears it; writing 0 has no
           effect. Capture which bits were cleared (set in value AND
           previously set in ossr_) and re-arm those channels' edge
           anchors against the current OSCR. */
        const uint32_t mask = value & 0xFu;
        const uint32_t prev = ossr_.fetch_and(~mask, std::memory_order_acq_rel);
        const uint32_t cleared = prev & mask;
        if (cleared == 0) return;

        const uint32_t oscr_now = ReadOscr();
        auto& intc = emu_.Get<Sa1110Intc>();
        for (int n = 0; n < 4; ++n) {
            if ((cleared & (1u << n)) == 0) continue;
            /* Re-arm osmr_arm[n]: keep osmr, replace oscr_at_arm with
               current OSCR. Use CAS so a concurrent JIT-thread OSMR
               write isn't clobbered. */
            uint64_t expected = osmr_arm_[n].load(std::memory_order_acquire);
            for (;;) {
                const uint32_t osmr = UnpackOsmr(expected);
                const uint64_t desired = PackOsmr(osmr, oscr_now);
                if (osmr_arm_[n].compare_exchange_weak(
                        expected, desired,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) break;
            }
            intc.DeassertSource(kIntcOst0Bit + n);
        }
        cv_.notify_all();
    }

    uint32_t ReadReg(uint32_t off) const {
        switch (off) {
            case 0x00: return UnpackOsmr(osmr_arm_[0].load(std::memory_order_acquire));
            case 0x04: return UnpackOsmr(osmr_arm_[1].load(std::memory_order_acquire));
            case 0x08: return UnpackOsmr(osmr_arm_[2].load(std::memory_order_acquire));
            case 0x0C: return UnpackOsmr(osmr_arm_[3].load(std::memory_order_acquire));
            case 0x10: return ReadOscr();
            case 0x14: return ossr_.load(std::memory_order_acquire) & 0xFu;
            case 0x18: return ower_.load(std::memory_order_acquire) & 0x1u;
            case 0x1C: return oier_.load(std::memory_order_acquire) & 0xFu;
            default:   return 0;
        }
    }

    void WriteReg(uint32_t off, uint32_t value) {
        switch (off) {
            case 0x00: WriteOsmr(0, value); break;
            case 0x04: WriteOsmr(1, value); break;
            case 0x08: WriteOsmr(2, value); break;
            case 0x0C: WriteOsmr(3, value); break;
            case 0x10: WriteOscr(value); break;
            case 0x14: WriteOssr(value); break;
            /* §9.4.3: WME is write-once — software cannot clear it. */
            case 0x18:
                ower_.fetch_or(value & 0x1u, std::memory_order_acq_rel);
                cv_.notify_all();
                break;
            case 0x1C:
                oier_.store(value & 0xFu, std::memory_order_release);
                cv_.notify_all();
                break;
            default: break;
        }
    }

    void CheckAndFire() {
        const uint32_t oier = oier_.load(std::memory_order_acquire);
        const uint32_t ossr_snap = ossr_.load(std::memory_order_acquire);
        uint32_t newly_set = 0;
        bool     trigger_reset = false;
        for (int n = 0; n < 4; ++n) {
            if ((oier      & (1u << n)) == 0) continue;
            if ((ossr_snap & (1u << n)) != 0) continue;
            /* DO NOT hoist ReadOscr above this load — JIT WriteOsmr/Ossr
               concurrently re-anchors oscr_at_arm fresher than a hoisted
               ReadOscr; forward_to_now then unsigned-wraps and match fires
               while OSCR < OSMR. */
            const uint64_t pair = osmr_arm_[n].load(std::memory_order_acquire);
            const uint32_t oscr = ReadOscr();
            if (!MatchHasFired(UnpackArm(pair), UnpackOsmr(pair), oscr)) continue;
            ossr_.fetch_or(1u << n, std::memory_order_acq_rel);
            newly_set |= (1u << n);
            /* §9.4.6: with OWER.WME=1, an OSMR3 match resets the SoC
               instead of asserting bit-29 IRQ. */
            if (n == 3 && (ower_.load(std::memory_order_acquire) & 0x1u) != 0) {
                trigger_reset = true;
            }
        }
        if (trigger_reset) {
            emu_.Get<ArmJit>().SetResetPending();
            return;
        }
        if (newly_set != 0) {
#if CERF_DEV_MODE
            emu_.Get<RateProbe>().Inc(RateProbe::Counter::OstFires);
#endif
            auto& intc = emu_.Get<Sa1110Intc>();
            for (int n = 0; n < 4; ++n) {
                if (newly_set & (1u << n)) {
                    intc.AssertSource(kIntcOst0Bit + n);
                }
            }
        }
    }

    bool AnyMatchArmed() const {
        const uint32_t oier      = oier_.load(std::memory_order_acquire);
        const uint32_t ossr_snap = ossr_.load(std::memory_order_acquire);
        return (oier & ~ossr_snap & 0xFu) != 0;
    }

    void MatchLoop() {
        std::unique_lock<std::mutex> lk(cv_mtx_);
        while (!stop_.load(std::memory_order_acquire)) {
            lk.unlock();
#if CERF_DEV_MODE
            emu_.Get<RateProbe>().Inc(RateProbe::Counter::OstPolls);
#endif
            RebaseToCurrent();
            CheckAndFire();
            lk.lock();
            if (stop_.load(std::memory_order_acquire)) break;
            if (AnyMatchArmed()) {
                cv_.wait_for(lk, kPollInterval);
            } else {
                cv_.wait(lk);
            }
        }
    }

    static bool IsKnown(uint32_t off) {
        return off == 0x00 || off == 0x04 || off == 0x08 || off == 0x0C ||
               off == 0x10 || off == 0x14 || off == 0x18 || off == 0x1C;
    }
};

uint8_t Sa1110OsTimer::ReadByte(uint32_t addr) {
#if CERF_DEV_MODE
    const uint64_t t0 = __rdtsc();
#endif
    const uint32_t off   = addr - MmioBase();
    const uint32_t base  = off & ~0x3u;
    const uint32_t shift = (off & 0x3u) * 8;
    if (!IsKnown(base)) HaltUnsupportedAccess("ReadByte", addr, 0);
    const uint8_t result = static_cast<uint8_t>((ReadReg(base) >> shift) & 0xFFu);
#if CERF_DEV_MODE
    emu_.Get<RateProbe>().AddTsc(RateProbe::TimeCounter::OstMmio, __rdtsc() - t0);
#endif
    return result;
}

uint32_t Sa1110OsTimer::ReadWord(uint32_t addr) {
#if CERF_DEV_MODE
    const uint64_t t0 = __rdtsc();
#endif
    const uint32_t off = addr - MmioBase();
    if (!IsKnown(off)) HaltUnsupportedAccess("ReadWord", addr, 0);
    const uint32_t result = ReadReg(off);
#if CERF_DEV_MODE
    emu_.Get<RateProbe>().AddTsc(RateProbe::TimeCounter::OstMmio, __rdtsc() - t0);
#endif
    return result;
}

void Sa1110OsTimer::WriteByte(uint32_t addr, uint8_t value) {
#if CERF_DEV_MODE
    const uint64_t t0 = __rdtsc();
#endif
    const uint32_t off   = addr - MmioBase();
    const uint32_t base  = off & ~0x3u;
    const uint32_t shift = (off & 0x3u) * 8;
    if (!IsKnown(base)) HaltUnsupportedAccess("WriteByte", addr, value);
    const uint32_t cur     = ReadReg(base);
    const uint32_t cleared = cur & ~(0xFFu << shift);
    WriteReg(base, cleared | (static_cast<uint32_t>(value) << shift));
#if CERF_DEV_MODE
    emu_.Get<RateProbe>().AddTsc(RateProbe::TimeCounter::OstMmio, __rdtsc() - t0);
#endif
}

void Sa1110OsTimer::WriteWord(uint32_t addr, uint32_t value) {
#if CERF_DEV_MODE
    const uint64_t t0 = __rdtsc();
#endif
    const uint32_t off = addr - MmioBase();
    if (!IsKnown(off)) HaltUnsupportedAccess("WriteWord", addr, value);
    WriteReg(off, value);
#if CERF_DEV_MODE
    emu_.Get<RateProbe>().AddTsc(RateProbe::TimeCounter::OstMmio, __rdtsc() - t0);
#endif
}

}  /* namespace */

REGISTER_SERVICE(Sa1110OsTimer);
