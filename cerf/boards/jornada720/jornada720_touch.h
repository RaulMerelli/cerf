#pragma once

#include "../../core/service.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

/* Jornada touch model. Holds the current raw 10-bit pen ADC (read by the MCU's
   GetTouchSamples handler) and drives the SA-1110 GPIO9 pen line that touch.dll
   watches (sub_FB1A98: GPLR bit 9 low = pen down/collect, high = pen up). A thin
   TouchInput adapter forwards host pointer events here. */
class Jornada720Touch : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    ~Jornada720Touch() override;

    /* Host pointer events (guest-surface coords), from the TouchInput adapter. */
    void PenDown(int x, int y);
    void PenMove(int x, int y);
    void PenUp();

    /* MCU GetTouchSamples (0xA0): current raw 10-bit ADC for both axes. */
    void CurrentAdc(uint16_t* adc_x, uint16_t* adc_y) const;

    /* Synthetic pen tap at a fixed RAW ADC point, bypassing host-coordinate
       mapping. For the bezel soft-button zones touch.dll watches (sub_FB1314:
       X 31..67 — below MapHostToAdc's X floor, unreachable by host clicks). */
    void TapRawAdc(uint16_t adc_x, uint16_t adc_y);

private:
    void     SamplerLoop();
    void     MapHostToAdc(int hx, int hy, uint16_t* ax, uint16_t* ay) const;
    void     DrivePenLine(bool pen_down);
    void     PulsePenLine();

    mutable std::mutex      mtx_;
    std::condition_variable cv_;
    std::thread             sampler_;
    bool                    stop_     = false;
    bool                    pen_down_ = false;
    int                     synthetic_pulses_ = 0;
    int                     last_x_   = 0;
    int                     last_y_   = 0;
    uint16_t                adc_x_    = 0;
    uint16_t                adc_y_    = 0;
};
