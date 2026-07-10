#pragma once

#include "../../peripherals/peripheral_base.h"
#include "../../state/state_stream.h"

#include <cstdint>

/* SA-11x0 Multimedia Communications Port. MCDR2 routes codec register reads/
   writes to a registered Sa11xxMcpCodec; MCCR0 holds the audio sample-rate
   divisor (ASD) the board audio player reads to set the host playback rate. */
class Sa11xxMcp : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x80060000u; }
    uint32_t MmioSize() const override { return 0x00000060u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* dev-man §11.12.3.1: rate = 11.981 MHz / 32 / ASD, ASD = MCCR0 bits 6:0
       (CFS=0 on the J820 - wavedev sets MCCR1=0). Returns the wavedev default
       11025 Hz until MCCR0 is programmed. */
    uint32_t GetAudioSampleRateHz() const;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    void RouteCodecCommand(uint32_t cmd);

    uint32_t mccr0_      = 0;
    uint32_t mcsr_       = 0;
    uint16_t mcdr2_read_ = 0;
};
