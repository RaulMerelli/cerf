#pragma once

#include "../peripheral_base.h"
#include "mediaq_mq200_ge.h"

#include <cstdint>
#include <vector>

/* MediaQ MQ200 display controller (SIMpad SL4). MmioBase 0x4B800000 = framebuffer
   SRAM (2 MB) at window offset 0; the register block (PA 0x4BE00000) is at offset
   0x600000. Display geometry is the Graphics Controller 1 block at register-area
   offset 0xA000 (MQ-200 Data Book Table 5-3), programmed by ddi.dll sub_1343898. */
class MediaQMq200 : public Peripheral, public MediaQGeHost {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x4B800000u; }
    uint32_t MmioSize() const override { return kRegWinOff + kRegSize; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* Display-state getters consumed by MediaQMq200Renderer. Values come from
       the Graphics Controller 1 register block (MQ-200 Data Book §5); the
       SIMpad driver writes them in sub_1343898/sub_13431F8/sub_1343798. */
    bool     IsEnabled()      const;   /* GC00R[3] image window on + valid mode. */
    uint32_t Bpp()            const;   /* decoded from GC00R[7:4] (Table 5-8). */
    uint32_t GetGuestW()      const;   /* GC08R[31:16] + 1. */
    uint32_t GetGuestH()      const;   /* GC09R[31:16] + 1. */
    uint32_t FbWindowOffset() const;   /* GC0CR[20:0], byte offset in FB SRAM. */
    uint32_t Stride()         const;   /* GC0ER[15:0], bytes per scanline. */
    uint32_t PaletteEntry(uint32_t index) const;   /* C100R+i: R[7:0] G[15:8] B[23:16]. */
    const uint8_t* FbBytes()  const { return fb_.data(); }
    uint8_t*       FbMutableBytes() override { return fb_.data(); }   /* GE blit target. */
    uint32_t       FbSize()   const override { return kFbSize; }

private:
    static constexpr uint32_t kFbSize    = 0x200000u;   /* sub_134BAA8 a1[65] */
    static constexpr uint32_t kRegWinOff = 0x600000u;   /* register block window offset */
    static constexpr uint32_t kRegSize   = 0x1A000u;    /* sub_134BAA8 a1[66] */

    /* PCI-config block (Table 5-3 @ 0x16000): ID gate + idle poll. */
    static constexpr uint32_t kRegId    = 0x16000u;     /* ID gate: == 0x02004D51 ("MQ"). */
    static constexpr uint32_t kRegIdle  = 0x16044u;     /* status; mode-set spins while [1:0] != 0. */
    static constexpr uint32_t kDeviceId = 0x02004D51u;

    /* CIF (Table 5-3 @ 0x2000) CC01R index 0x04 = Source/Command FIFO/GE Status
       (Data Book Table 5-86); ddi.dll busy-waits on it around each blit. The
       engine answers via MediaQMq200Ge::StatusReady(). */
    static constexpr uint32_t kRegGeStatus = 0x2004u;

    /* Graphics Engine (Table 5-3 @ 0xC000) register block + Source FIFO port. */
    static constexpr uint32_t kGeBlockLo = 0xC000u;
    static constexpr uint32_t kGeBlockHi = 0xC200u;
    static constexpr uint32_t kSrcFifo   = 0x18000u;

    /* Graphics Controller 1 register-area offsets (Table 5-3 base 0x0A000). */
    static constexpr uint32_t kGc00R = 0xA000u;  /* control: [0] ctrl-en, [3] img-win-en, [7:4] depth. */
    static constexpr uint32_t kGc08R = 0xA020u;  /* horizontal window: [31:16] = width - 1. */
    static constexpr uint32_t kGc09R = 0xA024u;  /* vertical window: [31:16] = height - 1. */
    static constexpr uint32_t kGc0CR = 0xA030u;  /* window start address [20:0]. */
    static constexpr uint32_t kGc0ER = 0xA038u;  /* window stride [15:0]. */
    static constexpr uint32_t kGc00ImgWinEnable = 0x8u;   /* GC00R[3] (Table 5-8). */

    static constexpr uint32_t kPaletteBase = 0x10000u;   /* Color Palette 1 (Table 5-3). */

    uint32_t Reg(uint32_t roff) const { return reg_[roff / 4u]; }
    uint32_t RegRead(uint32_t roff);
    void     RegWrite(uint32_t roff, uint32_t value);
    void     PublishScreenSizeOnEnableEdge();

    std::vector<uint8_t>  fb_;
    std::vector<uint32_t> reg_;
    bool     enable_published_ = false;
    uint32_t published_w_ = 0, published_h_ = 0;
    MediaQMq200Ge ge_{*this};
};
