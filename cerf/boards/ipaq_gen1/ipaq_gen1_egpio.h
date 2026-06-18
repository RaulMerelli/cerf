#pragma once

#include "../../peripherals/peripheral_base.h"

#include <atomic>
#include <cstdint>

class StateWriter;
class StateReader;

/* iPAQ H3600 EGPIO write-only latch (static bank 5, PA 0x49000000). Bit 0x400 is
   the audio-output amp enable driven by the wavedev (sub_F53D64): clear = output
   on, set = muted. Latched() lets the audio player gate playback on it. */
class IpaqGen1Egpio : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x49000000u; }
    uint32_t MmioSize() const override { return 0x00000004u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    uint32_t Latched() const { return latched_.load(std::memory_order_acquire); }

    static constexpr uint32_t kAudioOutputEnable = 0x400u;

private:
    void NotifySink();

    std::atomic<uint32_t> latched_{0};
};
