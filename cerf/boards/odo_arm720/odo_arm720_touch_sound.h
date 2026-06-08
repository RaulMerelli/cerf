#pragma once

#include "../../peripherals/peripheral_base.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

class OdoArm720TouchSound : public Peripheral {
public:
    using Peripheral::Peripheral;
    ~OdoArm720TouchSound() override;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    uint32_t MmioBase() const override { return 0x1000A000u; }
    uint32_t MmioSize() const override { return 0x20u; }

    uint16_t ReadHalf (uint32_t addr) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* Caller MUST follow with AssertIrq(kSourceTouchAudioAdcIntr)
       or kernel ISR never runs and playback hangs. Returns true
       if `bits` were already set — kernel end-of-stream signal
       per WAVEPDD.C:246-253; audio worker halts on this. */
    bool RaiseSoundStrBits(uint16_t bits);

    bool SoundIntrEnabled() const;
    bool PlaybackEnabled() const;

    void OnPenDown(int host_x, int host_y);
    void OnPenMove(int host_x, int host_y);
    void OnPenUp  ();

private:
    static const char* SlotName(uint32_t off);
    static uint16_t HostPixelToRaw(int host_v);
    uint16_t SlotRefLocked(uint32_t off, uint16_t*& out_ref);
    void NotifyAudioControlChange(uint16_t old_value, uint16_t new_value);
    void RecomputeTouchAudioIrq();
    bool ShouldTouchAudioBeLiveLocked() const;
    void DoAdcSampleLocked(uint16_t io_adc_cntr_write);
    void PenTimerMain();
    void StopPenTimerThread();

    mutable std::mutex state_mutex_;
    uint16_t io_adc_cntr_   = 0;
    uint16_t io_adc_str_    = 0;
    uint16_t ucb_cntr_      = 0;
    uint16_t ucb_str_       = 0;
    uint16_t ucb_register_  = 0;
    uint16_t io_sound_cntr_ = 0;
    uint16_t io_sound_str_  = 0;
    uint16_t intr_mask_     = 0;

    uint16_t ucb_regs_[16]  = {0};

    uint16_t adc_x_ = 0;
    uint16_t adc_y_ = 0;

    /* P2.H:403 penState bit 12: SET = pen up, CLEAR = pen down. */
    std::atomic<bool> pen_down_{false};

    std::thread             pen_timer_thread_;
    std::condition_variable pen_timer_cv_;
    std::mutex              pen_timer_cv_mtx_;
    std::atomic<bool>       pen_timer_enabled_{false};
    std::atomic<bool>       shutdown_{false};
};
