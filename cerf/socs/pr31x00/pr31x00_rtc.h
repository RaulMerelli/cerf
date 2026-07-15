#pragma once

#include "../../peripherals/peripheral_base.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

class Pr31x00Intc;

/* Philips PR31x00 Timer Module, TMPR3911/3912 ch.15. Registers $140-$157: the
   40-bit RTC counter, the 40-bit Alarm, Timer Control and the Periodic Timer. */
class Pr31x00Rtc : public Peripheral {
public:
    using Peripheral::Peripheral;

    ~Pr31x00Rtc() override { StopWorker(); }
    void OnShutdown() override { StopWorker(); }

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x10C00140u; }
    uint32_t MmioSize() const override { return 0x18u; }   /* $140-$157 */

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    uint8_t  ReadByte(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 RTC ReadByte", addr, 0); }
    uint16_t ReadHalf(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 RTC ReadHalf", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("PR31x00 RTC WriteByte", addr, v); }
    void WriteHalf(uint32_t addr, uint16_t v) override { HaltUnsupportedAccess("PR31x00 RTC WriteHalf", addr, v); }

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    using Clock    = std::chrono::steady_clock;
    using RtcTicks = std::chrono::duration<uint64_t, std::ratio<1, 32768>>;

    uint64_t          CountLocked() const;
    Clock::time_point TimeAtCountLocked(uint64_t target) const;
    Clock::duration   PeriodLocked() const;
    uint32_t          PerCntLocked() const;
    void              EvaluateLocked();
    void     NotifyWorker();
    void     StopWorker();
    void     WorkerLoop();

    mutable std::mutex mtx_;

    uint64_t          base_ticks_ = 0;
    Clock::time_point anchor_     = {};
    bool              rtc_clr_    = false;   /* RTCCLR holds the counter at zero */

    uint64_t alarm_          = 0;
    bool     alarm_armed_    = false;   /* ARARM resets to X; live once written */
    bool     alarm_fired_    = false;
    bool     rollover_fired_ = false;
    uint32_t timer_ctl_      = 0;

    uint16_t          perval_           = 0;       /* $154 PERVAL[15:0] reload */
    bool              periodic_enabled_ = false;   /* TimerCtl ENPERTIMER<4>   */
    Clock::time_point periodic_next_    = {};      /* next PERINT deadline     */

    Pr31x00Intc* intc_ = nullptr;

    std::mutex              cv_mtx_;
    std::condition_variable cv_;
    bool                    rearm_ = false;   /* guarded by cv_mtx_ */
    std::thread             worker_;
    std::atomic<bool>       stop_{false};
};
