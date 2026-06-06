#pragma once

#include "../peripheral_base.h"
#include "sed1356_bitblt.h"

#include <chrono>
#include <cstdint>
#include <vector>

/* EPSON SED1356 / S1D13506 Color LCD/CRT/TV Controller (Technical Manual
   X25B-A-001-12). Jornada 720: 640x240 16-bpp color STN at PA 0x48000000,
   512 KB display buffer at +0x200000. */
class Sed1356 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x48000000u; }
    uint32_t MmioSize() const override { return 0x00400000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* --- Display pipe state consumed by Sed1356Renderer (all §8.3). --- */
    bool     LcdDisplayOn() const;       /* REG[1FCh] bits 2:0 selects LCD.   */
    bool     LcdBlanked()   const { return (Reg(0x40) & 0x80u) != 0; }
    uint32_t LcdBpp()       const;       /* REG[040h] bits 2:0 -> 4/8/15/16.  */
    uint32_t LcdGuestW()    const { return ((Reg(0x32) & 0x7Fu) + 1u) * 8u; }
    uint32_t LcdGuestH()    const {
        return (((Reg(0x39) & 0x3u) << 8 | Reg(0x38)) & 0x3FFu) + 1u;
    }
    uint32_t LcdStartByte() const {      /* word address * 2 (§8.3.7 note).   */
        return ((Reg(0x44) & 0xFu) << 16 | Reg(0x43) << 8 | Reg(0x42)) * 2u;
    }
    uint32_t LcdStrideBytes() const {    /* 11-bit word offset (REG[046/047]). */
        return (((Reg(0x47) & 0x7u) << 8) | Reg(0x46)) * 2u;
    }
    uint32_t LcdPixelPan()  const { return Reg(0x48) & 0x3u; }
    uint32_t SwivelMode() const {        /* Table 8-19: 0/90/180/270 degrees. */
        return ((Reg(0x40) >> 4) & 1u) << 1 | ((Reg(0x1FC) >> 6) & 1u);
    }
    /* LCD LUT entry, 4-bit DAC components (§8.3.13). */
    void     LcdLutRgb(uint32_t index, uint8_t& r4, uint8_t& g4, uint8_t& b4) const;

    /* Ink/cursor layer (§8.3.10, §14). */
    uint32_t LcdInkCursorMode() const { return Reg(0x70) & 0x3u; }  /* 0 off, 1 cursor, 2 ink. */
    uint32_t LcdInkCursorStartByte() const;   /* Table 14-1 decode, bytes into VRAM. */
    uint32_t LcdCursorX() const { return ((Reg(0x73) & 0x3u) << 8) | Reg(0x72); }
    uint32_t LcdCursorY() const { return ((Reg(0x75) & 0x3u) << 8) | Reg(0x74); }
    bool     LcdCursorXNeg() const { return (Reg(0x73) & 0x80u) != 0; }
    bool     LcdCursorYNeg() const { return (Reg(0x75) & 0x80u) != 0; }
    /* Ink/cursor colors 0/1 as 5:6:5 components (REG[076h..07Ch]). */
    void     LcdInkColor(uint32_t which, uint8_t& r5, uint8_t& g6, uint8_t& b5) const;

    /* --- BitBLT engine surface (regs §8.3.12, latched by Sed1356BitBlt). --- */
    uint8_t  BltReg(uint32_t off) const { return Reg(off); }
    uint8_t* VramData()       { return vram_.data(); }
    uint32_t VramSize() const { return kVramSize; }
    uint32_t VramMask() const { return kVramMask; }

private:
    static constexpr uint32_t kRegWindow      = 0x200u;
    static constexpr uint32_t kMediaPlugBase  = 0x1000u;
    static constexpr uint32_t kMediaPlugEnd   = 0x100Au;
    static constexpr uint32_t kBltAperture    = 0x100000u;
    static constexpr uint32_t kBltApertureEnd = 0x200000u;
    /* The display-buffer address space is always 2 MB (AB[20:0]); the Jornada's
       512 KB physical buffer is replicated across it (TM X25B-A-001 §10,
       Table 10-1 / Fig 10-1; HP doc §2) — accesses alias mod kVramSize. */
    static constexpr uint32_t kVramBase       = 0x200000u;
    static constexpr uint32_t kVramAperture   = 0x200000u;
    static constexpr uint32_t kVramSize       = 0x80000u;
    static constexpr uint32_t kVramMask       = kVramSize - 1u;

    uint8_t  Reg(uint32_t off) const { return reg_[off]; }
    uint8_t  RegRead (uint32_t off);
    void     RegWrite(uint32_t off, uint8_t value);
    uint8_t  ReadLutData();
    void     WriteLutData(uint8_t value);
    void     StepLutPointer();
    uint8_t  VndStatusBit() const;
    void     PublishOnLcdEnableEdge();

    uint8_t reg_[kRegWindow] = {};
    std::vector<uint8_t> vram_;

    /* Two 256-entry RGB LUTs of 4-bit components (§8.3.13: LCD + CRT/TV).
       lut_rgb_latch_ holds R,G written before the committing B write. */
    uint8_t lcd_lut_[256][3] = {};
    uint8_t crt_lut_[256][3] = {};
    uint8_t lut_index_     = 0;   /* entry pointer (REG[1E2h] write resets). */
    uint8_t lut_component_ = 0;   /* 0=R 1=G 2=B, auto-increments (§8.3.13). */
    uint8_t lut_rgb_latch_[2] = {};

    bool     enable_published_ = false;
    uint32_t published_w_ = 0, published_h_ = 0;

    std::chrono::steady_clock::time_point boot_time_{};

    Sed1356BitBlt blt_{*this};

    friend class Sed1356BitBlt;
};
