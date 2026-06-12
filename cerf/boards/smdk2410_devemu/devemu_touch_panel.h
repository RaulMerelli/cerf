#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>
#include <mutex>

class DevEmuTouchPanel : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x58000000u; }
    uint32_t MmioSize() const override { return 0x00000014u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    /* Called from the TouchInput adapter on host mouse events.
       Coordinates are host-window client-area pixels. */
    void OnPenDown    (int host_x, int host_y);
    void OnPenMove    (int host_x, int host_y);
    void OnPenUp      (int host_x, int host_y);
    void OnCaptureLost();

private:
    /* Helpers — all assume state_mutex_ held by caller. */
    void SetPenStateLocked (bool pen_up);
    void UpdateSampleLocked(int host_x, int host_y);

    /* Raises sub-source INT_TC; rolls up to main INT_ADC inside
       IrqController. Must be called outside state_mutex_ — IrqController
       has its own lock and we avoid nesting host locks across services. */
    void RaiseTCInterrupt();

    mutable std::mutex state_mutex_;

    /* Register state. Reset values per IOADConverter::Reset in the BSP
       (ADCCON=0x3FC4, ADCTSC=0x58, ADCDLY=0xFF, ADCDAT0/1=0). UPDOWN
       in ADCDAT0/1 is set in OnReady so the kernel reads pen-up on
       initial probe. */
    uint32_t adccon_  = 0x3FC4u;
    uint32_t adctsc_  = 0x0058u;
    uint32_t adcdly_  = 0x00FFu;
    uint32_t adcdat0_ = 0u;
    uint32_t adcdat1_ = 0u;

    /* Latest sampled pen position in the BSP's empirically-derived
       10-bit range (SampleX 90..965, SampleY 50..920). Published into
       ADCDAT0.XPDATA / ADCDAT1.YPDATA only when the guest writes
       ADCCON.ENABLE_START to kick off an auto-conversion. */
    uint16_t sample_x_ = 0;
    uint16_t sample_y_ = 0;
};
