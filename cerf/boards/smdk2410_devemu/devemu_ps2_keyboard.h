#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>
#include <mutex>

class DevEmuPs2Keyboard : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x59000020u; }
    uint32_t MmioSize() const override { return 0x00000018u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void OnHostKey(uint8_t vk, bool key_up);

private:
    void EnqueueScancode(uint8_t sc);

    mutable std::mutex state_mutex_;

    /* Reset values per IOSPI's ctor in the BSP: SPSTA starts with
       REDY=1 (Tx ready), SPPIN with bit 1 (reserved-but-required)=1. */
    uint32_t spcon_  = 0;
    uint32_t spsta_  = 1;
    uint32_t sppin_  = 2;
    uint32_t sppre_  = 0;
    uint8_t  sptdat_ = 0;
    uint8_t  sprdat_ = 0;

    static constexpr int kQueueLen = 16;
    uint8_t queue_[kQueueLen]{};
    int     queue_head_ = 0;
    int     queue_tail_ = 0;

    bool num_lock_state_ = true;
};
