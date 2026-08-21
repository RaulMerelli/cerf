#pragma once

#include "../peripherals/peripheral_base.h"

#include "guest_cpu_reset.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../core/rate_probe.h"
#include "../core/virtual_clock.h"
#include "../core/virtual_timer_list.h"
#include "../peripherals/peripheral_dispatcher.h"
#include "../state/state_stream.h"

#include <cstdint>
#include <atomic>
#include <mutex>

namespace cerf_os_timer_detail {
constexpr uint64_t Gcd(uint64_t a, uint64_t b) {
    return b == 0u ? a : Gcd(b, a % b);
}
}

/* Intel/Marvell OS Timer - the same IP block on SA-1110 (§9.4) and PXA25x
   (§4.4): OSCR / OSMR0-3 / OSSR / OWER / OIER at 0x00..0x1C
   (SA-1110 §9.4.7 Table 9-1). */
class OsTimer : public Peripheral {
public:
    using Peripheral::Peripheral;

    void OnReady() override {
        auto& timers = emu_.Get<VirtualTimerList>();
        for (int n = 0; n < 4; ++n) {
            entry_[n] = timers.Add([this, n] { OnDeadline(n); });
        }
        const int64_t now = emu_.Get<VirtualClock>().NowNs();
        SetAnchor(now, 0u);
        ArmAll(now);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            std::lock_guard<std::mutex> g(reg_mtx_);
            OnResetLine();
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioSize() const override { return 0x00001000u; }

    FastReadFn  FastReader() override { return &OsTimer::FastReadThunk; }
    FastWriteFn FastWriter() override { return &OsTimer::FastWriteThunk; }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (!IsKnown(off)) HaltUnsupportedAccess("ReadWord", addr, 0);
        std::lock_guard<std::mutex> g(reg_mtx_);
        return ReadReg(off);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (!IsKnown(off)) HaltUnsupportedAccess("WriteWord", addr, value);
        std::lock_guard<std::mutex> g(reg_mtx_);
        WriteReg(off, value);
    }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> g(reg_mtx_);
        for (int n = 0; n < 4; ++n) w.Write<uint32_t>(osmr_[n]);
        w.Write<uint32_t>(ossr_);
        w.Write<uint32_t>(ower_);
        w.Write<uint32_t>(oier_);
        w.Write<uint32_t>(OscrAtNs(NowNs()));
    }

    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> g(reg_mtx_);
        for (int n = 0; n < 4; ++n) {
            uint32_t v = 0;
            r.Read(v);
            osmr_[n].store(v, std::memory_order_release);
        }
        r.Read(ossr_);
        r.Read(ower_);
        r.Read(oier_);
        uint32_t oscr = 0;
        r.Read(oscr);
        const int64_t now = NowNs();
        SetAnchor(now, oscr);
        ArmAll(now);
    }

    void PostRestore() override {
        std::lock_guard<std::mutex> g(reg_mtx_);
        PushMatchLevel();
    }

protected:
    /* SA-1110 §9.4.2: the OSSR status bits are routed to the interrupt
       controller. §9.4.5: OIER gates only the SET of an OSSR bit - clearing an
       enable bit does not clear a set status bit, so OIER is not in the level. */
    virtual void SetMatchLevel(uint32_t level4) = 0;

    /* SA-1110 §9.4.6: a reset "clears most internal states", exempting only the
       power manager, refresh timer and PLL configuration; §9.6 lists the same
       exemptions per reset kind. The §9.4.5 OIER and §9.4.3 OWER reset rows are
       0, where §9.4.4 uses '?' for a value unknown at reset (OSSR M3..M0). */
    virtual void OnResetLine() {
        ower_ = 0;
        oier_ = 0;
    }

    /* PXA255 Tables 4-41 / 4-42 / 4-44 / 4-45 reset rows: OSMR0-3, the OIER
       E3..E0 bits, OSCR and the OSSR M3..M0 bits all reset to 0. */
    void ResetRegistersToZero() {
        for (int n = 0; n < 4; ++n) osmr_[n] = 0;
        ossr_ = 0;
        oier_ = 0;
        const int64_t now = NowNs();
        SetAnchor(now, 0u);
        ArmAll(now);
        PushMatchLevel();
    }

private:
    /* SA-1110 §9.4.1: the OSCR "increments on rising edges of the 3.6864-MHz
       clock"; PXA255 §4.4.2.4 gives the same rate. A dedicated oscillator -
       never derived from the CPU clock. */
    static constexpr uint32_t kOscrHz = 3686400u;

    static constexpr int64_t kOscrWrapNs =
        (4294967296ll * 1000000000ll) / kOscrHz;

    static bool IsKnown(uint32_t off) {
        return off == 0x00 || off == 0x04 || off == 0x08 || off == 0x0C ||
               off == 0x10 || off == 0x14 || off == 0x18 || off == 0x1C;
    }

    static uint32_t FastReadThunk(void* ctx, uint32_t off, uint32_t width) {
        return static_cast<OsTimer*>(ctx)->FastRead(off, width);
    }
    static void FastWriteThunk(void* ctx, uint32_t off, uint32_t value, uint32_t width) {
        static_cast<OsTimer*>(ctx)->FastWrite(off, value, width);
    }

    int64_t NowNs() const { return emu_.Get<VirtualClock>().NowNs(); }

    static constexpr uint64_t kNsPerSec  = 1000000000ull;
    static constexpr uint64_t kScaleGcd  =
        cerf_os_timer_detail::Gcd(kNsPerSec, kOscrHz);
    static constexpr uint64_t kNsPerUnit = kNsPerSec / kScaleGcd;
    static constexpr uint64_t kTkPerUnit = kOscrHz / kScaleGcd;

    static int64_t TicksToNs(uint32_t ticks) {
        return static_cast<int64_t>(
            static_cast<uint64_t>(ticks) * kNsPerUnit / kTkPerUnit);
    }
    static uint32_t NsToTicks(int64_t ns) {
        return static_cast<uint32_t>(
            static_cast<uint64_t>(ns) * kTkPerUnit / kNsPerUnit);
    }

    void SetAnchor(int64_t ns, uint32_t oscr) {
        anchor_seq_.fetch_add(1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        anchor_ns_.store(ns, std::memory_order_relaxed);
        oscr_anchor_.store(oscr, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        anchor_seq_.fetch_add(1, std::memory_order_relaxed);
    }

    uint32_t OscrAtNs(int64_t now) const {
        for (;;) {
            const uint32_t s0 = anchor_seq_.load(std::memory_order_acquire);
            if ((s0 & 1u) != 0u) continue;
            const int64_t  ns = anchor_ns_.load(std::memory_order_relaxed);
            const uint32_t oc = oscr_anchor_.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (anchor_seq_.load(std::memory_order_relaxed) != s0) continue;
            return oc + NsToTicks(now - ns);
        }
    }

    /* SA-1110 §9.4.2: each OSMR is compared against the OSCR following every
       rising edge of the 3.6864-MHz clock. */
    int64_t NextMatchNs(int n, int64_t now, uint32_t oscr_now) const {
        const uint32_t ticks = osmr_[n] - oscr_now;
        return now + (ticks != 0 ? TicksToNs(ticks) : kOscrWrapNs);
    }

    void ArmChannel(int n, int64_t now, uint32_t oscr_now) {
        entry_[n]->Arm(NextMatchNs(n, now, oscr_now));
    }

    void ArmAll(int64_t now) {
        const uint32_t oscr_now = OscrAtNs(now);
        for (int n = 0; n < 4; ++n) {
            ArmChannel(n, now, oscr_now);
        }
    }

    void PushMatchLevel() {
        SetMatchLevel(ossr_ & 0xFu);
    }

    void OnDeadline(int n) {
        std::lock_guard<std::mutex> g(reg_mtx_);
        if (entry_[n]->DeadlineNs() != VirtualTimerList::kNoDeadline) return;
        /* SA-1110 §9.4.2: the OSMRs are compared against the OSCR on every
           rising edge - the comparator does not stop at a match - and a match
           at that time sets the corresponding OSSR status bit. */
        const int64_t  now  = NowNs();
        const uint32_t oscr = OscrAtNs(now);
        ArmChannel(n, now, oscr);
        if (static_cast<int32_t>(osmr_[n] - oscr) > 0) return;
        /* SA-1110 §9.4.5: the OIER enables decide whether a match will set a
           status bit in the OSSR - for every match register, with no WME term. */
        if ((oier_ & (1u << n)) != 0) {
            ossr_ |= (1u << n);
            PushMatchLevel();
#if CERF_DEV_MODE
            emu_.Get<RateProbe>().Inc(RateProbe::Counter::OstFires);
#endif
        }
        /* SA-1110 §9.4.3 OWER bit 0 (WME): 0 - OSMR3 matches cause an interrupt
           request; 1 - OSMR3 matches cause a reset of the SA-1110. §9.4.6 and
           PXA255 §4.4.1 enable that reset on OWER[0], with no OIER term. */
        if (n == 3 && (ower_ & 0x1u) != 0) {
            emu_.Get<GuestCpuReset>().WatchdogReset();
        }
    }

    uint32_t ReadReg(uint32_t off) {
        switch (off) {
            case 0x00: return osmr_[0];
            case 0x04: return osmr_[1];
            case 0x08: return osmr_[2];
            case 0x0C: return osmr_[3];
            case 0x10:
#if CERF_DEV_MODE
                emu_.Get<RateProbe>().Inc(RateProbe::Counter::OstReadOscr);
#endif
                return OscrAtNs(NowNs());
            case 0x14: return ossr_ & 0xFu;
            case 0x18: return ower_ & 0x1u;
            case 0x1C: return oier_ & 0xFu;
        }
        HaltUnsupportedAccess("ReadReg", MmioBase() + off, 0);
    }

    void WriteReg(uint32_t off, uint32_t value) {
        switch (off) {
            case 0x00: case 0x04: case 0x08: case 0x0C: {
                const int n = static_cast<int>(off >> 2);
                const int64_t now = NowNs();
                osmr_[n] = value;
                ArmChannel(n, now, OscrAtNs(now));
                return;
            }
            case 0x10: {
                const int64_t now = NowNs();
                SetAnchor(now, value);
                ArmAll(now);
                return;
            }
            /* SA-1110 §9.4.4: an OSSR bit is cleared by writing a one to it;
               writing zeros has no effect. */
            case 0x14:
                ossr_ &= ~(value & 0xFu);
                PushMatchLevel();
                return;
            /* SA-1110 §9.4.3: WME is a write-once bit that can only be changed
               by a hardware, software or sleep-mode reset. */
            case 0x18:
                ower_ |= (value & 0x1u);
                return;
            case 0x1C:
                oier_ = value & 0xFu;
                return;
        }
        HaltUnsupportedAccess("WriteReg", MmioBase() + off, value);
    }

    /* jornada720 nk.exe (OST via 0x8802A134: sub_80075384, sub_80075638) and
       falcon_4220__4_10 nk.exe (OST at 0xBAF00000: sub_800F33D4, sub_800F76A0)
       access the OST word-only; a sub-word RMW of the W1C OSSR clears set
       status bits (SA-1110 §9.4.4). */
    uint32_t FastRead(uint32_t off, uint32_t width) {
        if (width != 4 || !IsKnown(off)) {
            HaltUnsupportedAccess("FastRead", MmioBase() + off, 0);
        }
        if (off <= 0x0Cu) {
            return osmr_[off >> 2].load(std::memory_order_acquire);
        }
        if (off == 0x10u) {
#if CERF_DEV_MODE
            emu_.Get<RateProbe>().Inc(RateProbe::Counter::OstReadOscr);
#endif
            return OscrAtNs(NowNs());
        }
        std::lock_guard<std::mutex> g(reg_mtx_);
        return ReadReg(off);
    }

    void FastWrite(uint32_t off, uint32_t value, uint32_t width) {
        if (width != 4 || !IsKnown(off)) {
            HaltUnsupportedAccess("FastWrite", MmioBase() + off, value);
        }
        std::lock_guard<std::mutex> g(reg_mtx_);
        WriteReg(off, value);
    }

    VirtualTimerList::Entry* entry_[4] = {};

    mutable std::mutex reg_mtx_;

    std::atomic<uint32_t> anchor_seq_{0};
    std::atomic<int64_t>  anchor_ns_{0};
    std::atomic<uint32_t> oscr_anchor_{0};
    std::atomic<uint32_t> osmr_[4] = {};
    uint32_t ossr_        = 0;
    uint32_t ower_        = 0;
    uint32_t oier_        = 0;
};
