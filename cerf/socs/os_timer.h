#pragma once

#include "../peripherals/peripheral_base.h"

#include "guest_cpu_reset.h"

#include "../core/cerf_emulator.h"
#include "../core/rate_probe.h"
#include "../core/virtual_clock.h"
#include "../core/virtual_timer_list.h"
#include "../peripherals/peripheral_dispatcher.h"
#include "../state/state_stream.h"

#include <cstdint>

/* Intel/Marvell OS Timer - the same IP block on SA-1110 (§9.4) and PXA25x
   (§4.4): OSCR / OSMR0-3 / OSSR / OWER / OIER at 0x00..0x1C
   (SA-1110 §9.4.7 Table 9-1). A per-SoC concrete supplies MmioBase +
   SetMatchLevel + ShouldRegister. */
class OsTimer : public Peripheral {
public:
    using Peripheral::Peripheral;

    void OnReady() override {
        auto& timers = emu_.Get<VirtualTimerList>();
        for (int n = 0; n < 4; ++n) {
            entry_[n] = timers.Add([this, n] { OnDeadline(n); });
        }
        anchor_ns_ = emu_.Get<VirtualClock>().NowNs();
        emu_.Get<GuestCpuReset>().RegisterResetListener(
            [this](ResetLineKind) { OnResetLine(); });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioSize() const override { return 0x00001000u; }

    FastReadFn  FastReader() override { return &OsTimer::FastReadThunk; }
    FastWriteFn FastWriter() override { return &OsTimer::FastWriteThunk; }

    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t off   = addr - MmioBase();
        const uint32_t base  = off & ~0x3u;
        const uint32_t shift = (off & 0x3u) * 8;
        if (!IsKnown(base)) HaltUnsupportedAccess("ReadByte", addr, 0);
        return static_cast<uint8_t>((ReadReg(base) >> shift) & 0xFFu);
    }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (!IsKnown(off)) HaltUnsupportedAccess("ReadWord", addr, 0);
        return ReadReg(off);
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t off   = addr - MmioBase();
        const uint32_t base  = off & ~0x3u;
        const uint32_t shift = (off & 0x3u) * 8;
        if (!IsKnown(base)) HaltUnsupportedAccess("WriteByte", addr, value);
        const uint32_t cur = ReadReg(base);
        WriteReg(base, (cur & ~(0xFFu << shift)) |
                           (static_cast<uint32_t>(value) << shift));
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (!IsKnown(off)) HaltUnsupportedAccess("WriteWord", addr, value);
        WriteReg(off, value);
    }

    void SaveState(StateWriter& w) override {
        for (int n = 0; n < 4; ++n) w.Write<uint32_t>(osmr_[n]);
        w.Write<uint32_t>(ossr_);
        w.Write<uint32_t>(ower_);
        w.Write<uint32_t>(oier_);
        w.Write<uint32_t>(OscrAtNs(NowNs()));
    }

    void RestoreState(StateReader& r) override {
        for (int n = 0; n < 4; ++n) r.Read(osmr_[n]);
        r.Read(ossr_);
        r.Read(ower_);
        r.Read(oier_);
        uint32_t oscr = 0;
        r.Read(oscr);
        const int64_t now = NowNs();
        anchor_ns_   = now;
        oscr_anchor_ = oscr;
        ArmAll(now);
    }

    void PostRestore() override { PushMatchLevel(); }

protected:
    /* SA-1110 §9.4.2: the OSSR status bits are routed to the interrupt
       controller. §9.4.5: OIER gates only the SET of an OSSR bit - clearing an
       enable bit does not clear a set status bit, so OIER is not in the level. */
    virtual void SetMatchLevel(uint32_t level4) = 0;

    /* SA-1110 §9.4.3 + PXA255 Table 4-43: WME cleared by every reset kind.
       SA-1110 §9.4.5 + PXA255 Table 4-42 reset rows: OIER E3..E0 reset to 0. */
    virtual void OnResetLine() {
        ower_ = 0;
        oier_ = 0;
    }

    /* PXA255 Tables 4-41 / 4-44 / 4-45 reset rows: OSMR0-3, OSCR and the OSSR
       M3..M0 bits all reset to 0. */
    void ResetCountersToZero() {
        for (int n = 0; n < 4; ++n) osmr_[n] = 0;
        ossr_ = 0;
        const int64_t now = NowNs();
        anchor_ns_   = now;
        oscr_anchor_ = 0;
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

    static int64_t TicksToNs(uint32_t ticks) {
        return static_cast<int64_t>(
            VirtualClock::ScaleU64(ticks, 1000000000ull, kOscrHz));
    }
    static uint32_t NsToTicks(int64_t ns) {
        return static_cast<uint32_t>(VirtualClock::ScaleU64(
            static_cast<uint64_t>(ns), kOscrHz, 1000000000ull));
    }

    uint32_t OscrAtNs(int64_t now) const {
        return oscr_anchor_ + NsToTicks(now - anchor_ns_);
    }

    /* SA-1110 §9.4.2: each OSMR is compared against the OSCR following every
       rising edge of the 3.6864-MHz clock. */
    int64_t NextMatchNs(int n, int64_t now, uint32_t oscr_now) const {
        const uint32_t ticks = osmr_[n] - oscr_now;
        return now + (ticks != 0 ? TicksToNs(ticks) : kOscrWrapNs);
    }

    void ArmAll(int64_t now) {
        const uint32_t oscr_now = OscrAtNs(now);
        for (int n = 0; n < 4; ++n) {
            entry_[n]->Arm(NextMatchNs(n, now, oscr_now));
        }
    }

    void PushMatchLevel() { SetMatchLevel(ossr_ & 0xFu); }

    void OnDeadline(int n) {
        /* SA-1110 §9.4.2: the OSMRs are compared against the OSCR on every
           rising edge - the comparator does not stop at a match. */
        {
            const int64_t now = NowNs();
            entry_[n]->Arm(NextMatchNs(n, now, OscrAtNs(now)));
        }
        /* SA-1110 §9.4.3 OWER bit 0 (WME): 0 - OSMR3 matches cause an interrupt
           request; 1 - OSMR3 matches cause a reset of the SA-1110. §9.4.6 and
           PXA255 §4.4.1 enable that reset on OWER[0], with no OIER term. */
        if (n == 3 && (ower_ & 0x1u) != 0) {
            emu_.Get<GuestCpuReset>().WatchdogReset();
            return;
        }
        /* SA-1110 §9.4.5: the OIER enables decide whether a match will set a
           status bit in the OSSR. */
        if ((oier_ & (1u << n)) == 0) return;
        ossr_ |= (1u << n);
        PushMatchLevel();
#if CERF_DEV_MODE
        emu_.Get<RateProbe>().Inc(RateProbe::Counter::OstFires);
#endif
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
                osmr_[n] = value;
                const int64_t now = NowNs();
                entry_[n]->Arm(NextMatchNs(n, now, OscrAtNs(now)));
                return;
            }
            case 0x10: {
                const int64_t now = NowNs();
                anchor_ns_   = now;
                oscr_anchor_ = value;
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
            WriteReg(base, (ReadReg(base) & ~(mask << shift)) |
                               ((value & mask) << shift));
        }
    }

    VirtualTimerList::Entry* entry_[4] = {};

    int64_t  anchor_ns_   = 0;
    uint32_t oscr_anchor_ = 0;
    uint32_t osmr_[4]     = {};
    uint32_t ossr_        = 0;
    uint32_t ower_        = 0;
    uint32_t oier_        = 0;
};
