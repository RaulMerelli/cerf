#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../core/log.h"
#include "../../core/virtual_clock.h"
#include "../../core/virtual_timer_list.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"
#include "../irq_controller.h"
#include "s3c2410_clocks.h"

#include <cstdint>
#include <mutex>

namespace {

struct TimerBits {
    int start;
    int manual_update;
    int auto_reload;
};
/* S3C2410A UM p.10-13/10-14 TCON: start/stop, manual update and auto reload
   bits per timer; timer 4 carries no output-inverter bit. */
constexpr TimerBits kTcon[5] = {
    { 0,   1,   3  },
    { 8,   9,   11 },
    { 12,  13,  15 },
    { 16,  17,  19 },
    { 20,  21,  22 },
};

/* S3C2410A User Manual, printed p. 14-7: SRCPND INT_TIMER0 [10] .. INT_TIMER4
   [14]. */
constexpr int kIrqTimerN[5] = { 10, 11, 12, 13, 14 };

/* S3C2410A UM p.10-15/10-19: TCNTBn, TCMPBn and TCNTOn are all [15:0]. */
constexpr uint32_t kCountMask = 0xFFFFu;

class S3C2410Timer : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::S3C2410;
    }
    void OnReady() override {
        auto& timers = emu_.Get<VirtualTimerList>();
        for (int i = 0; i < 5; ++i) {
            entry_[i] = timers.Add([this, i] { OnDeadline(i); });
        }
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            std::lock_guard<std::mutex> lk(state_mutex_);
            OnResetLine();
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x51000000u; }
    uint32_t MmioSize() const override { return 0x00100000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    enum class RegKind { Tcfg0, Tcfg1, Tcon, TcntbN, TcmpbN, TcntoN, OutOfRange };
    struct DecodedReg { RegKind kind; int timer_idx; };

    static DecodedReg DecodeReg(uint32_t offset);

    static int64_t TicksToNs(uint32_t ticks, uint64_t freq_hz) {
        return static_cast<int64_t>(
            static_cast<uint64_t>(ticks) * 1000000000ull / freq_hz);
    }
    static uint32_t NsToTicks(int64_t ns, uint64_t freq_hz) {
        if (ns <= 0) return 0;
        return static_cast<uint32_t>(
            static_cast<uint64_t>(ns) * freq_hz / 1000000000ull);
    }

    int64_t NowNs() const { return emu_.Get<VirtualClock>().NowNs(); }

    uint64_t TimerFreqHz(int n) const;
    uint32_t CountAtLocked(int n, int64_t now) const;
    void     ReanchorLocked(int n, int64_t now);
    void     ArmLocked(int n);
    void     ApplyTconWrite(uint32_t new_tcon, int64_t now);

    void OnDeadline(int n);
    void OnResetLine();

    struct TimerState {
        uint32_t tcntb       = 0;
        uint32_t tcmpb       = 0;
        bool     running     = false;
        bool     auto_reload = false;
        /* S3C2410A UM p.10-3: TCNTn is the internal down counter that TCNTOn
           reads. */
        int64_t  anchor_ns   = 0;
        uint32_t count       = 0;
    };

    mutable std::mutex       state_mutex_;
    uint32_t                 tcfg0_ = 0;
    uint32_t                 tcfg1_ = 0;
    uint32_t                 tcon_  = 0;
    TimerState               timers_[5];
    VirtualTimerList::Entry* entry_[5] = {};
};

S3C2410Timer::DecodedReg S3C2410Timer::DecodeReg(uint32_t offset) {
    if (offset == 0x00u) return { RegKind::Tcfg0, 0 };
    if (offset == 0x04u) return { RegKind::Tcfg1, 0 };
    if (offset == 0x08u) return { RegKind::Tcon,  0 };
    /* S3C2410A UM p.10-15: TCNTB0 0x5100000C, TCMPB0 0x51000010,
       TCNTO0 0x51000014; timers 1..3 repeat the triple every 0x0C. */
    for (int i = 0; i < 4; ++i) {
        const uint32_t base = 0x0Cu + 0x0Cu * static_cast<uint32_t>(i);
        if (offset == base + 0u) return { RegKind::TcntbN, i };
        if (offset == base + 4u) return { RegKind::TcmpbN, i };
        if (offset == base + 8u) return { RegKind::TcntoN, i };
    }
    /* S3C2410A UM p.10-19: TCNTB4 0x5100003C and TCNTO4 0x51000040; timer 4
       has no compare buffer. */
    if (offset == 0x3Cu) return { RegKind::TcntbN, 4 };
    if (offset == 0x40u) return { RegKind::TcntoN, 4 };
    return { RegKind::OutOfRange, 0 };
}

uint64_t S3C2410Timer::TimerFreqHz(int n) const {
    /* S3C2410A UM p.10-11: "Timer input clock Frequency = PCLK / {prescaler
       value+1} / {divider value}"; TCFG0[7:0] prescales timers 0 and 1,
       TCFG0[15:8] prescales timers 2, 3 and 4. */
    const uint64_t presc = (n <= 1)
        ? ((tcfg0_      ) & 0xFFu) + 1u
        : ((tcfg0_ >> 8 ) & 0xFFu) + 1u;

    /* S3C2410A UM p.10-12 TCFG1: MUXn 0000/0001/0010/0011 select 1/2, 1/4,
       1/8 and 1/16; 01xx selects the external TCLK input. */
    const uint32_t mux = (tcfg1_ >> (n * 4)) & 0xFu;
    if (mux >= 4u) {
        emu_.Get<Fatal>().Die("S3C2410Timer: timer %d MUX %u selects external "
                              "TCLK, which CERF does not model", n, mux);
    }
    return kS3C2410PclkHz / presc / (1ull << (mux + 1));
}

uint32_t S3C2410Timer::CountAtLocked(int n, int64_t now) const {
    const TimerState& t = timers_[n];
    if (!t.running) return t.count;
    const uint64_t freq    = TimerFreqHz(n);
    const int64_t  elapsed = now - t.anchor_ns;
    if (elapsed >= TicksToNs(t.count, freq)) return 0;
    return t.count - NsToTicks(elapsed, freq);
}

void S3C2410Timer::ReanchorLocked(int n, int64_t now) {
    timers_[n].count     = CountAtLocked(n, now);
    timers_[n].anchor_ns = now;
}

void S3C2410Timer::ArmLocked(int n) {
    TimerState& t = timers_[n];
    if (!t.running) {
        entry_[n]->Arm(VirtualTimerList::kNoDeadline);
        return;
    }
    entry_[n]->Arm(t.anchor_ns + TicksToNs(t.count, TimerFreqHz(n)));
}

void S3C2410Timer::ApplyTconWrite(uint32_t new_tcon, int64_t now) {
    for (int i = 0; i < 5; ++i) {
        const TimerBits& bits = kTcon[i];
        TimerState& t = timers_[i];
        /* S3C2410A UM p.10-13 TCON start/stop bits [0]/[8]/[12]/[16]/[20]:
           "0 = Stop, 1 = Start", with the "cleared at next writing" note
           carried only on the manual update bits. */
        const bool start  = ((new_tcon >> bits.start)         & 1u) != 0;
        const bool manual = ((new_tcon >> bits.manual_update) & 1u) != 0;

        t.auto_reload = ((new_tcon >> bits.auto_reload) & 1u) != 0;

        /* S3C2410A UM p.10-5: the starting value of TCNTn is loaded by the
           manual update bit. */
        if (manual) {
            t.count     = t.tcntb;
            t.anchor_ns = now;
        }
        if (!start) {
            /* S3C2410A UM p.10-5: "If the timer is stopped by force, the TCNTn
               retains the counter value and is not reloaded from TCNTBn." */
            if (t.running) t.count = CountAtLocked(i, now);
            t.anchor_ns = now;
            t.running   = false;
        } else if (!t.running && t.count != 0) {
            t.anchor_ns = now;
            t.running   = true;
        }
        ArmLocked(i);
    }
}

/* S3C2410A UM p.10-11/10-12/10-13/10-15/10-19: TCFG0, TCFG1, TCON, TCNTBn,
   TCMPBn and TCNTOn all carry a reset value of 0x00000000. */
void S3C2410Timer::OnResetLine() {
    tcfg0_ = 0;
    tcfg1_ = 0;
    tcon_  = 0;
    for (int i = 0; i < 5; ++i) {
        timers_[i] = TimerState{};
        entry_[i]->Arm(VirtualTimerList::kNoDeadline);
    }
}

void S3C2410Timer::OnDeadline(int n) {
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        if (entry_[n]->DeadlineNs() != VirtualTimerList::kNoDeadline) return;
        TimerState& t = timers_[n];
        if (!t.running) return;

        const int64_t reached_zero_ns =
            t.anchor_ns + TicksToNs(t.count, TimerFreqHz(n));
        t.anchor_ns = reached_zero_ns;
        /* S3C2410A UM p.10-4: auto reload copies TCNTBn into TCNTn at 0, and
           with the auto reload bit 0 "the TCNTn does not operate any
           further". */
        t.count = t.auto_reload ? t.tcntb : 0u;
        if (t.count == 0) {
            t.running = false;
            entry_[n]->Arm(VirtualTimerList::kNoDeadline);
        } else {
            ArmLocked(n);
        }
    }
    /* S3C2410A UM p.10-3: "When the TCNTn reaches 0, an interrupt request will
       occur if the interrupt is enabled." */
    emu_.Get<IrqController>().AssertIrq(kIrqTimerN[n]);
}

uint32_t S3C2410Timer::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    const auto dec = DecodeReg(off);

    std::lock_guard<std::mutex> lk(state_mutex_);
    switch (dec.kind) {
        case RegKind::Tcfg0:  return tcfg0_;
        case RegKind::Tcfg1:  return tcfg1_;
        case RegKind::Tcon:   return tcon_;
        /* S3C2410A UM p.10-4: a TCNTBn read returns the reload value for the
           next timer duration, not the state of the counter. */
        case RegKind::TcntbN: return timers_[dec.timer_idx].tcntb;
        case RegKind::TcmpbN: return timers_[dec.timer_idx].tcmpb;
        case RegKind::TcntoN: return CountAtLocked(dec.timer_idx, NowNs());
        case RegKind::OutOfRange:
            HaltUnsupportedAccess("ReadWord", addr, 0);
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);  /* noreturn */
}

void S3C2410Timer::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    const auto dec = DecodeReg(off);

    std::lock_guard<std::mutex> lk(state_mutex_);
    switch (dec.kind) {
        case RegKind::Tcfg0: {
            LOG(SocTimer, "S3C2410Timer: TCFG0 <- 0x%08X\n", value);
            const int64_t now = NowNs();
            for (int i = 0; i < 5; ++i) ReanchorLocked(i, now);
            tcfg0_ = value;
            for (int i = 0; i < 5; ++i) ArmLocked(i);
            break;
        }
        case RegKind::Tcfg1: {
            LOG(SocTimer, "S3C2410Timer: TCFG1 <- 0x%08X\n", value);
            /* S3C2410A UM p.10-12 TCFG1 [23:20] DMA mode: 0001..0101 route the
               selected timer's request to the DMA controller, and only 0000
               leaves every timer on the interrupt controller. */
            const uint32_t dma_mode = (value >> 20) & 0xFu;
            if (dma_mode != 0u) {
                emu_.Get<Fatal>().Die(
                    "S3C2410Timer: TCFG1 DMA mode %u routes a timer request "
                    "to the DMA controller, which CERF does not model",
                    dma_mode);
            }
            const int64_t now = NowNs();
            for (int i = 0; i < 5; ++i) ReanchorLocked(i, now);
            tcfg1_ = value;
            for (int i = 0; i < 5; ++i) ArmLocked(i);
            break;
        }
        case RegKind::Tcon:
#if CERF_DEV_MODE
            LOG(SocTimer, "S3C2410Timer: TCON 0x%08X -> 0x%08X\n", tcon_, value);
#endif
            ApplyTconWrite(value, NowNs());
            tcon_ = value;
            break;
        case RegKind::TcntbN:
#if CERF_DEV_MODE
            LOG(SocTimer, "S3C2410Timer: TCNTB%d <- 0x%X\n", dec.timer_idx,
                value & kCountMask);
#endif
            timers_[dec.timer_idx].tcntb = value & kCountMask;
            break;
        case RegKind::TcmpbN:
            timers_[dec.timer_idx].tcmpb = value & kCountMask;
            break;
        /* S3C2410A UM p.10-15: TCNTOn is read-only. */
        case RegKind::TcntoN:
            break;
        case RegKind::OutOfRange:
            HaltUnsupportedAccess("WriteWord", addr, value);
    }
}

void S3C2410Timer::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    const int64_t now = NowNs();
    w.Write(tcfg0_);
    w.Write(tcfg1_);
    w.Write(tcon_);
    for (int i = 0; i < 5; ++i) {
        const TimerState& t = timers_[i];
        w.Write(t.tcntb);
        w.Write(t.tcmpb);
        w.Write<uint8_t>(t.running ? 1u : 0u);
        w.Write<uint8_t>(t.auto_reload ? 1u : 0u);
        w.Write<uint32_t>(CountAtLocked(i, now));
    }
}

void S3C2410Timer::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    const int64_t now = NowNs();
    r.Read(tcfg0_);
    r.Read(tcfg1_);
    r.Read(tcon_);
    for (int i = 0; i < 5; ++i) {
        TimerState& t = timers_[i];
        r.Read(t.tcntb);
        r.Read(t.tcmpb);
        uint8_t running = 0, auto_reload = 0;
        r.Read(running);
        r.Read(auto_reload);
        r.Read(t.count);
        t.running     = (running != 0);
        t.auto_reload = (auto_reload != 0);
        t.anchor_ns   = now;
        ArmLocked(i);
    }
}

}  /* namespace */

REGISTER_SERVICE(S3C2410Timer);
