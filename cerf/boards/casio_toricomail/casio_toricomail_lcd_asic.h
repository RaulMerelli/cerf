#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>
#include <vector>

/* "LCD/High-Speed System Bus Area 0x0A000000-0x0AFFFFFF" (VR4121 UM Figure 6-8).
   Register set from nk.exe StartUp 0x9F0B5BC4, sub_9F0B6E78, sub_9F0B7D20,
   sub_9F0B7DE0, ISR 0x9F0BA2C4. */
class CasioToricomailLcdAsic : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kWindowSize; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    /* nk.exe sub_9F0B7DE0: pixel at 2*(x + (y<<9)) + 0xAA200000, row advance -2*w + 1024
       => framebuffer PA 0x0A200000, 1024-byte pitch (512 px), 16bpp. */
    static constexpr uint32_t kBase       = 0x0A000000u;
    static constexpr uint32_t kWindowSize = 0x01000000u;
    static constexpr uint32_t kFbOffset   = 0x00200000u;
    static constexpr uint32_t kFbSize     = 0x00040000u;
    static constexpr uint32_t kPitchBytes = 1024u;
    static constexpr uint32_t kVisibleW   = 320u;
    static constexpr uint32_t kVisibleH   = 240u;

    bool     InFb(uint32_t off) const { return off >= kFbOffset && off < kFbOffset + kFbSize; }
    uint16_t ReadReg(uint32_t off);
    void     WriteReg(uint32_t off, uint16_t value);
    void     RunFillLocked();

    std::vector<uint8_t> fb_;

    /* Fill engine (sub_9F0B7D20): dst/value/width/height + the 0x21C/0x21E config the
       guest always writes 1/0 before a fill. */
    uint16_t fill_dst_    = 0;
    uint16_t fill_value_  = 0;
    uint16_t fill_width_  = 0;
    uint16_t fill_height_ = 0;

    /* nk.exe ISR 0x9F0BA2C4: interrupt STATUS 0x1008 / ENABLE 0x100A / 0x100C; causes 0xE2
       drive GIU pin 9 (SYSINTR 16). No modeled source raises a cause yet. */
    uint16_t int_status_ = 0;
    uint16_t int_enable_ = 0;

    /* StartUp (0x9F0B5BCC) 0x1002=0x108, 0x1010=3, 0x1134=0; display-init sub_9F0B6E78
       0x7A2/0x7A4/0x7A0, 0x40A posted-write flush, 0x406 3-wire DAC (RMW'd by power-down). */
    uint16_t reg_1002_ = 0;
    uint16_t reg_1010_ = 0;
    uint16_t reg_1134_ = 0;
    uint16_t reg_7a0_  = 0;
    uint16_t reg_7a2_  = 0;
    uint16_t reg_7a4_  = 0;
    uint16_t reg_40a_  = 0;
    uint16_t reg_406_  = 0;

    /* nk.exe sub_9F0B8720 suspend: reads 0x010-0x01E; RMW 0x1130. */
    uint16_t suspend_save_[8] = {};
    uint16_t reg_1130_ = 0;
};
