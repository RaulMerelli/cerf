#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../irq_controller.h"
#include "../../boards/board_detector.h"
#include "omap3530_intc.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

namespace {

constexpr uint32_t kGptimer1BasePa  = 0x48318000u;
constexpr uint32_t kGptimer1Size    = 0x00001000u;  /* 4 KB */
constexpr int      kIrqGptimer1     = 37;            /* INTC source */
constexpr uint64_t kClockHz         = 32768ull;     /* 32 kHz reference */

/* Register offsets within the 4 KB window. */
constexpr uint32_t kOffTidr   = 0x00;
constexpr uint32_t kOffTiocp  = 0x10;
constexpr uint32_t kOffTistat = 0x14;
constexpr uint32_t kOffTisr   = 0x18;
constexpr uint32_t kOffTier   = 0x1C;
constexpr uint32_t kOffTwer   = 0x20;
constexpr uint32_t kOffTclr   = 0x24;
constexpr uint32_t kOffTcrr   = 0x28;
constexpr uint32_t kOffTldr   = 0x2C;
constexpr uint32_t kOffTtgr   = 0x30;
constexpr uint32_t kOffTwps   = 0x34;
constexpr uint32_t kOffTmar   = 0x38;
constexpr uint32_t kOffTcar1  = 0x3C;
constexpr uint32_t kOffTsicr  = 0x40;
constexpr uint32_t kOffTcar2  = 0x44;
constexpr uint32_t kOffTpir   = 0x48;
constexpr uint32_t kOffTnir   = 0x4C;
constexpr uint32_t kOffTcvr   = 0x50;
constexpr uint32_t kOffTocr   = 0x54;
constexpr uint32_t kOffTowr   = 0x58;

/* TIOCP bits. */
constexpr uint32_t kTiocpSoftReset = 1u << 1;

/* TISR / TIER / TWER bit layout — TCAR, OVF, MAT. */
constexpr uint32_t kIntMat = 1u << 0;
constexpr uint32_t kIntOvf = 1u << 1;
constexpr uint32_t kIntTcar = 1u << 2;
constexpr uint32_t kIntMask = kIntMat | kIntOvf | kIntTcar;

/* TCLR bits. */
constexpr uint32_t kTclrSt = 1u << 0;
constexpr uint32_t kTclrAr = 1u << 1;
constexpr uint32_t kTclrCe = 1u << 6;

class Omap3530Gptimer1 : public Peripheral {
public:
    using Peripheral::Peripheral;
    ~Omap3530Gptimer1() override {
        stop_thread_.store(true, std::memory_order_release);
        if (tick_thread_.joinable()) tick_thread_.join();
    }

    bool ShouldRegister() override {
        return emu_.Get<BoardDetector>().GetSoc() == SocFamily::OMAP3530;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        tick_thread_ = std::thread(&Omap3530Gptimer1::TickLoop, this);
    }

    uint32_t MmioBase() const override { return kGptimer1BasePa; }
    uint32_t MmioSize() const override { return kGptimer1Size; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

private:
    using Clock = std::chrono::steady_clock;

    /* Caller holds state_mutex_. Computes the interpolated TCRR at
       wall-clock time `now`, treating the counter as 32-bit
       free-running at kClockHz when TCLR.ST is set. */
    uint32_t ComputeTcrrLocked(Clock::time_point now) const;

    /* Worker entry — wakes every 1 ms, advances TCRR, dispatches
       MATCH / OVF events, edges the IRQ line. */
    void TickLoop();

    /* Caller holds state_mutex_. Recompute the IRQ line state and
       drive AssertIrq / DeAssertIrq on edges. */
    void RecomputeIrqLineLocked();

    /* Caller holds state_mutex_. Process whatever happened between
       last_processed_tcrr_ and (the freshly-computed) new_tcrr
       given the current TCLR / TIER state: detect MATCH crossing,
       detect overflow, apply auto-reload. */
    void AdvanceStateLocked(Clock::time_point now);

    /* Caller holds state_mutex_. Reset every per-timer register
       to its post-reset default. Called on TIOCP.SOFTRESET. */
    void ApplySoftResetLocked(Clock::time_point now);

    /* Caller holds state_mutex_. Reset the TCRR sampling base to
       (value, now). Called from TCRR writes and from TCLR.ST
       transitions. */
    void SampleTcrrBaseLocked(uint32_t value, Clock::time_point now);

    /* Caller holds state_mutex_. Match-distance check in 32-bit
       modular arithmetic: returns true iff TMAR sits in
       (old_tcrr, new_tcrr] when delta = new - old (mod 2^32)
       advances through the comparator. */
    static bool MatchCrossed(uint32_t old_tcrr, uint32_t new_tcrr,
                             uint32_t tmar);

    mutable std::mutex state_mutex_;

    uint32_t tiocp_   = 0;
    uint32_t tisr_    = 0;
    uint32_t tier_    = 0;
    uint32_t twer_    = 0;
    uint32_t tclr_    = 0;
    uint32_t tldr_    = 0;
    uint32_t ttgr_    = 0;
    uint32_t tmar_    = 0xFFFFFFFFu;
    uint32_t tsicr_   = 0;

    /* TCRR base-sample. ComputeTcrrLocked turns these into the
       interpolated current value. */
    uint32_t          tcrr_base_           = 0;
    Clock::time_point tcrr_base_time_      = {};
    uint32_t          last_processed_tcrr_ = 0;

    /* True iff the IRQ output line to INTC source 37 is currently
       driven high. Tracked so we issue AssertIrq / DeAssertIrq
       on edges, not on every recompute. */
    bool irq_line_high_ = false;

    std::thread       tick_thread_;
    std::atomic<bool> stop_thread_{false};
};

bool Omap3530Gptimer1::MatchCrossed(uint32_t old_tcrr, uint32_t new_tcrr,
                                    uint32_t tmar) {
    /* Distance from old_tcrr to TMAR (modulo 2^32). 0 means TMAR
       sits at old_tcrr exactly — we don't fire on that because
       the previous wake already would have. */
    const uint32_t to_match = tmar - old_tcrr;
    const uint32_t advanced = new_tcrr - old_tcrr;
    if (to_match == 0) return false;
    return advanced >= to_match;
}

uint32_t Omap3530Gptimer1::ComputeTcrrLocked(Clock::time_point now) const {
    if ((tclr_ & kTclrSt) == 0) {
        return tcrr_base_;
    }
    const auto elapsed_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            now - tcrr_base_time_).count();
    if (elapsed_ns <= 0) return tcrr_base_;
    const uint64_t elapsed_ticks =
        static_cast<uint64_t>(elapsed_ns) * kClockHz / 1'000'000'000ull;
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(tcrr_base_) + elapsed_ticks) & 0xFFFFFFFFull);
}

void Omap3530Gptimer1::SampleTcrrBaseLocked(uint32_t value,
                                            Clock::time_point now) {
    tcrr_base_           = value;
    tcrr_base_time_      = now;
    last_processed_tcrr_ = value;
}

void Omap3530Gptimer1::ApplySoftResetLocked(Clock::time_point now) {
    tiocp_  = 0;
    tisr_   = 0;
    tier_   = 0;
    twer_   = 0;
    tclr_   = 0;
    tldr_   = 0;
    ttgr_   = 0;
    tmar_   = 0xFFFFFFFFu;
    tsicr_  = 0;
    SampleTcrrBaseLocked(0, now);
    /* Don't clear irq_line_high_ — caller's RecomputeIrqLineLocked
       needs its pre-reset value to detect the high→low edge and
       issue DeAssertIrq(37); clearing it strands ITR[37] high. */
}

void Omap3530Gptimer1::RecomputeIrqLineLocked() {
    const bool want_high = (tisr_ & tier_ & kIntMask) != 0;
    if (want_high == irq_line_high_) return;
    irq_line_high_ = want_high;
    auto& intc = emu_.Get<IrqController>();
    if (want_high) intc.AssertIrq  (kIrqGptimer1);
    else           intc.DeAssertIrq(kIrqGptimer1);
}

void Omap3530Gptimer1::AdvanceStateLocked(Clock::time_point now) {
    if ((tclr_ & kTclrSt) == 0) return;

    const uint32_t new_tcrr = ComputeTcrrLocked(now);
    const uint32_t old_tcrr = last_processed_tcrr_;
    if (new_tcrr == old_tcrr) return;

    /* Match crossing — only if CE is set. */
    if ((tclr_ & kTclrCe) != 0 && MatchCrossed(old_tcrr, new_tcrr, tmar_)) {
        tisr_ |= kIntMat;
    }

    /* Overflow detection: in modular arithmetic, overflow occurred
       iff new_tcrr < old_tcrr (the counter wrapped through 0). */
    if (new_tcrr < old_tcrr) {
        tisr_ |= kIntOvf;
        if ((tclr_ & kTclrAr) != 0) {
            /* Auto-reload: re-sample TCRR base at TLDR. We re-anchor
               the base time at `now` and let the next wake
               interpolate from there. */
            SampleTcrrBaseLocked(tldr_, now);
            return;
        }
        /* If AR is 0, real silicon keeps counting from 0 after
           overflow. Our model already reflects that — new_tcrr
           wrapped and ComputeTcrrLocked will continue from
           there. Just record the wake. */
    }

    last_processed_tcrr_ = new_tcrr;
}

void Omap3530Gptimer1::TickLoop() {
    while (!stop_thread_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        std::lock_guard<std::mutex> lk(state_mutex_);
        AdvanceStateLocked(Clock::now());
        RecomputeIrqLineLocked();
    }
}

uint32_t Omap3530Gptimer1::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    std::lock_guard<std::mutex> lk(state_mutex_);
    const auto now = Clock::now();
    AdvanceStateLocked(now);
    /* Log only TCRR reads with their values — that's what matters. */
    if (off == kOffTcrr) {
        static uint32_t last_tcrr = 0;
        const uint32_t v = ComputeTcrrLocked(now);
        if (v != last_tcrr) {
            LOG(Periph, "[GPTIMER1] TCRR=0x%08X (delta=%d) tclr=0x%02X\n",
                v, (int32_t)(v - last_tcrr), tclr_);
            last_tcrr = v;
        }
    }

    switch (off) {
    case kOffTidr:    return 0u;                                  /* module ID */
    case kOffTiocp:   return tiocp_;
    case kOffTistat:  return 0x1u;                                /* RESETDONE */
    case kOffTisr:    return tisr_ & kIntMask;
    case kOffTier:    return tier_ & kIntMask;
    case kOffTwer:    return twer_ & kIntMask;
    case kOffTclr:    return tclr_;
    case kOffTcrr:    return ComputeTcrrLocked(now);
    case kOffTldr:    return tldr_;
    case kOffTtgr:    return ttgr_;
    case kOffTwps:    return 0u;                                  /* never busy */
    case kOffTmar:    return tmar_;
    case kOffTsicr:   return tsicr_;
    /* Capture / fractional / overflow-counter registers: not
       modelled, return 0. Writes halt loudly below. */
    case kOffTcar1:   return 0u;
    case kOffTcar2:   return 0u;
    case kOffTpir:    return 0u;
    case kOffTnir:    return 0u;
    case kOffTcvr:    return 0u;
    case kOffTocr:    return 0u;
    case kOffTowr:    return 0u;
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);  /* noreturn */
}

void Omap3530Gptimer1::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    std::lock_guard<std::mutex> lk(state_mutex_);
    const auto now = Clock::now();
    AdvanceStateLocked(now);
    LOG(Periph, "[GPTIMER1] W off=0x%02X <- 0x%08X\n", off, value);

    switch (off) {
    case kOffTidr:
        /* Module ID is read-only. */
        return;
    case kOffTiocp:
        if (value & kTiocpSoftReset) {
            ApplySoftResetLocked(now);
            RecomputeIrqLineLocked();
            return;
        }
        tiocp_ = value;
        return;
    case kOffTistat:
        /* Status — read-only. */
        return;
    case kOffTisr:
        /* W1C: clear any bit the caller wrote 1 to. */
        tisr_ &= ~(value & kIntMask);
        RecomputeIrqLineLocked();
        return;
    case kOffTier:
        tier_ = value & kIntMask;
        LOG(Periph, "[GPTIMER1] TIER <- 0x%X (MAT=%d OVF=%d)\n",
            tier_, (tier_ & kIntMat) ? 1 : 0, (tier_ & kIntOvf) ? 1 : 0);
        RecomputeIrqLineLocked();
        return;
    case kOffTwer:
        twer_ = value & kIntMask;
        return;
    case kOffTclr: {
        const bool old_st = (tclr_ & kTclrSt) != 0;
        const bool new_st = (value & kTclrSt) != 0;
        if (!old_st && new_st) {
            /* 0->1 ST: start counting from the current tcrr_base_
               value at the current wall-clock time. */
            SampleTcrrBaseLocked(tcrr_base_, now);
        } else if (old_st && !new_st) {
            /* 1->0 ST: freeze the counter at its current
               interpolated value. */
            const uint32_t frozen = ComputeTcrrLocked(now);
            tcrr_base_           = frozen;
            tcrr_base_time_      = now;
            last_processed_tcrr_ = frozen;
        }
        tclr_ = value;
        /* No match/overflow recompute here — the next wake or
           read will handle it. */
        return;
    }
    case kOffTcrr:
        SampleTcrrBaseLocked(value, now);
        return;
    case kOffTldr:
        tldr_ = value;
        return;
    case kOffTtgr:
        /* Writing TTGR triggers a reload from TLDR. */
        ttgr_ = value;
        SampleTcrrBaseLocked(tldr_, now);
        return;
    case kOffTwps:
        /* Posted-write status — read-only. */
        return;
    case kOffTmar:
        tmar_ = value;
        return;
    case kOffTsicr:
        tsicr_ = value;
        return;
    case kOffTpir:
    case kOffTnir:
        if (value != 0) {
            HaltUnsupportedAccess(
                "WriteWord(TPIR/TNIR non-zero — fractional clock "
                "increment not modelled)",
                addr, value);
        }
        return;
    case kOffTcar1:
    case kOffTcar2:
        /* Capture registers — read-only on real silicon. */
        return;
    case kOffTcvr:
    case kOffTocr:
    case kOffTowr:
        /* Overflow-counter / wakeup registers — not modelled
           because the boot path doesn't program them. Halt if
           the kernel ever writes here. */
        HaltUnsupportedAccess(
            "WriteWord(TCVR/TOCR/TOWR — overflow-counter family "
            "not modelled)",
            addr, value);
    }
    HaltUnsupportedAccess("WriteWord", addr, value);  /* noreturn */
}

}  /* namespace */

REGISTER_SERVICE(Omap3530Gptimer1);
