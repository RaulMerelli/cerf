#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_detector.h"
#include "../../boards/siemens_mp377/siemens_mp377_touch_panel.h"
#include "../../core/cerf_emulator.h"
#include "../../socs/irq_controller.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <array>
#include <cstdint>

/* IOP13xx Peripheral Memory-Mapped Register (PMMR) catch-all.
   IOP13xx Developer Manual §3.3.1: 1 MB region at PA 0xFFD80000 hosts
   ATU0/1, MU, ADMA0/1, DMA0/1, ATIB, I2C0/1/2, UART0/1, GPIO, ESSR,
   debug. The early P377 OAL touches DMA1 (PMMR + 0x2480) before any
   driver routes IRQs, and the upcoming bring-up steps will surface
   ESSR / GPIO probes from the same region.

   The PMMR is split into stub windows so they don't overlap the modeled
   blocks the dispatcher would otherwise reject: Iop13xxUart at
   [0x2340..0x2370) and the ATU outbound configuration-cycle registers
   OCCAR/OCCDR at [0x4D330..0x4D338). PeripheralDispatcher rejects
   overlapping registrations rather than disambiguating by depth, so the
   stubs flank the modeled blocks. As real peripherals get modeled they
   each carve out their own MmioBase / MmioSize. */
namespace {

constexpr uint32_t kPmmrBase    = 0xFFD80000u;
constexpr uint32_t kUartOffset  = 0x00002340u;   /* Iop13xxUart MmioBase */
constexpr uint32_t kUartSize    = 0x00000030u;   /* Iop13xxUart MmioSize */
constexpr uint32_t kPmmrEnd     = kPmmrBase + 0x00100000u;

/* ATU outbound configuration cycle registers, pinned by instrumentation of
   the P377 OAL: OCCAR (config-cycle address) at PMMR+0x4D330, OCCDR
   (config-cycle data) at PMMR+0x4D334. The OAL writes OCCAR with bit 31 set
   plus a device/function select and a register offset in the low byte, then
   reads OCCDR for the dword. */
constexpr uint32_t kOccarOffset = 0x0004D330u;
constexpr uint32_t kAtuCfgBase  = kPmmrBase + kOccarOffset;   /* 0xFFDCD330 */
constexpr uint32_t kAtuCfgSize  = 0x00000008u;                /* OCCAR + OCCDR */

/* IOP13xx I2C unit 0, pinned from i2c.dll (register pointer 0xFFD82500).
   ICR @ +0x00, ISR @ +0x04, IDBR @ +0x0C. The board's RTC8564 sits on this
   bus and the i2c.dll transfer routines poll ISR after every byte. */
constexpr uint32_t kI2cBase = kPmmrBase + 0x00002500u;        /* 0xFFD82500 */
constexpr uint32_t kI2cSize = 0x00000020u;

class PmmrStub : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetSoc() == SocFamily::IOP13xx;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint8_t  ReadByte (uint32_t) override { return 0; }
    uint16_t ReadHalf (uint32_t) override { return 0; }
    uint32_t ReadWord (uint32_t addr) override {
        if ((addr & ~3u) == kPmmrBase + 0x00002484u) {
            auto* bd = emu_.TryGet<BoardDetector>();
            if (bd && bd->GetBoard() == Board::SiemensMP377) {
                const uint32_t v = siemens_mp377::Mp377TouchReadPenDetectReg();
                emu_.Get<IrqController>().DeAssertIrq(siemens_mp377::kMp377TouchIrqSource);
                return v;
            }
        }
        return 0;
    }
    void     WriteByte(uint32_t, uint8_t)  override {}
    void     WriteHalf(uint32_t, uint16_t) override {}
    void     WriteWord(uint32_t, uint32_t) override {}
};

class Iop13xxPmmrStubLow : public PmmrStub {
public:
    using PmmrStub::PmmrStub;
    uint32_t MmioBase() const override { return kPmmrBase; }
    uint32_t MmioSize() const override { return kUartOffset; }
};

class Iop13xxPmmrStubMid : public PmmrStub {
public:
    using PmmrStub::PmmrStub;
    uint32_t MmioBase() const override { return kPmmrBase + kUartOffset + kUartSize; }
    uint32_t MmioSize() const override { return kI2cBase - MmioBase(); }
};

/* Between the I2C unit and the ATU config registers. */
class Iop13xxPmmrStubMid2 : public PmmrStub {
public:
    using PmmrStub::PmmrStub;
    uint32_t MmioBase() const override { return kI2cBase + kI2cSize; }
    uint32_t MmioSize() const override { return kAtuCfgBase - MmioBase(); }
};

/* IOP13xx I2C unit. The i2c.dll byte-transfer routines write the data byte to
   IDBR, set the Transfer-Byte bit (0x08) in ICR, then poll ISR: the transmit
   path (sub_2B4174C) waits for ISR bit 0x40, the receive path (sub_2B417FC)
   for bit 0x80; neither tolerates the receive-full (0x20) or bus-error (0x400)
   bits during the wait. Reporting ISR = 0xC0 (both done bits, no error)
   completes every byte immediately so the RTC8564 probe and any other bus
   client stop spinning. IDBR reads return 0 (no device-specific data
   modelled yet), which the clients fall back on. */
class Iop13xxI2c : public PmmrStub {
public:
    using PmmrStub::PmmrStub;
    uint32_t MmioBase() const override { return kI2cBase; }
    uint32_t MmioSize() const override { return kI2cSize; }

    uint32_t ReadWord(uint32_t addr) override {
        switch (addr - kI2cBase) {
            case 0x04: return 0x000000C0u;   /* ISR: transmit + receive done */
            case 0x0C: return 0x00000000u;   /* IDBR: dummy receive data */
            default:   return 0u;
        }
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
};

/* Past the ATU config registers to the end of the PMMR. */
class Iop13xxPmmrStubHigh : public PmmrStub {
public:
    using PmmrStub::PmmrStub;
    uint32_t MmioBase() const override { return kAtuCfgBase + kAtuCfgSize; }
    uint32_t MmioSize() const override { return kPmmrEnd - MmioBase(); }
};

/* IOP13xx ATU outbound configuration-cycle window. Latches the config
   address written to OCCAR and answers OCCDR reads with PCI configuration
   space for the modeled devices. For the Siemens MP 377 the only device is
   the Silicon Motion Voyager GX (SM501) display controller the BSP scans
   for (vendor 0x126F, device 0x501, class 0x03). Empty slots read back
   0xFFFFFFFF so the BSP scan skips them. */
class Iop13xxAtuConfig : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetSoc() == SocFamily::IOP13xx;
    }
    void OnReady() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        is_mp377_ = bd && bd->GetBoard() == Board::SiemensMP377;
        BuildSm501Config();
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kAtuCfgBase; }
    uint32_t MmioSize() const override { return kAtuCfgSize; }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - kAtuCfgBase;
        if (off == 0)  return occar_;                /* OCCAR readback */
        if (off == 4)  return ConfigData();          /* OCCDR */
        return 0xFFFFFFFFu;
    }
    void WriteWord(uint32_t addr, uint32_t v) override {
        const uint32_t off = addr - kAtuCfgBase;
        if (off == 0) {
            occar_ = v;
        } else if (off == 4) {
            ConfigWrite(v);                           /* OCCDR write */
        }
    }
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t w = ReadWord(addr & ~3u);
        return static_cast<uint16_t>((w >> ((addr & 2u) * 8u)) & 0xFFFFu);
    }
    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t w = ReadWord(addr & ~3u);
        return static_cast<uint8_t>((w >> ((addr & 3u) * 8u)) & 0xFFu);
    }
    void WriteHalf(uint32_t, uint16_t) override {}
    void WriteByte(uint32_t, uint8_t)  override {}

private:
    /* Device/function select carried by OCCAR, with the enable bit and the
       register byte masked off. The OAL probes select 0x007800. */
    static uint32_t DeviceSelect(uint32_t occar) {
        return occar & 0x7FFFFF00u;
    }
    static constexpr uint32_t kSm501Select = 0x00007800u;

    uint32_t ConfigData() const {
        if (!is_mp377_) return 0xFFFFFFFFu;
        if (DeviceSelect(occar_) != kSm501Select) return 0xFFFFFFFFu;
        const uint32_t reg = (occar_ & 0xFFu) >> 2;
        if (reg >= sm501_cfg_.size()) return 0xFFFFFFFFu;
        return sm501_cfg_[reg];
    }
    void ConfigWrite(uint32_t v) {
        if (!is_mp377_ || DeviceSelect(occar_) != kSm501Select) return;
        const uint32_t reg = (occar_ & 0xFFu) >> 2;
        if (reg < 4 || reg > 6) { /* only BARs are writable here */
            return;
        }
        /* BAR sizing: a write of all ones returns the size mask on read. */
        if (v == 0xFFFFFFFFu) {
            sm501_cfg_[reg] = bar_size_mask_[reg - 4];
        } else {
            sm501_cfg_[reg] = (v & bar_size_mask_[reg - 4]) | bar_flags_[reg - 4];
        }
    }

    void BuildSm501Config() {
        sm501_cfg_.fill(0);
        sm501_cfg_[0] = 0x0501126Fu;   /* device 0x0501 | vendor 0x126F */
        sm501_cfg_[1] = 0x02000007u;   /* status 0x0200 | command 0x0007 */
        sm501_cfg_[2] = 0x03000000u;   /* class 0x03 (display) sub 0 progif 0 rev 0 */
        sm501_cfg_[3] = 0x00000000u;   /* header type 0 */
        /* The IOP13xx ATU maps these outbound PCI addresses 1:1 to CPU
           physical, so the BAR values double as the PA the BSP dereferences.
           They sit in the free band between the primary (..0xC8000000) and
           secondary (0xD0000000..) ATU windows, modelled by the board's
           SM501 framebuffer / register peripherals.
           BAR1 control registers (mem, 2 MB) @ 0xC8000000;
           BAR0 framebuffer (prefetchable mem, 8 MB) @ 0xCA000000. */
        sm501_cfg_[4] = 0xCA000008u;
        sm501_cfg_[5] = 0xC8000000u;
        sm501_cfg_[0xB] = 0x0501126Fu; /* subsystem id */
        /* Interrupt Pin = A, Interrupt Line = 0. byte[obj+76] in smibase is
           not actually copied from this field — sub_2B51544 leaves v3[16]
           zero-initialised, so dev+76 byte 0 reads 0 regardless. Keeping
           Interrupt Line at 0 preserves the value the BSP scan saw on the
           build that reaches ddi_vgx (memory: "DISPLAY NOW WORKS"). */
        sm501_cfg_[0xF] = 0x00000100u;
    }

    bool is_mp377_ = false;
    uint32_t occar_ = 0;
    std::array<uint32_t, 64> sm501_cfg_{};
    /* BAR0 = 16 MB framebuffer, BAR1 = 2 MB registers. */
    const std::array<uint32_t, 3> bar_size_mask_{ 0xFF000000u, 0xFFE00000u, 0u };
    const std::array<uint32_t, 3> bar_flags_{ 0x8u, 0x0u, 0u };
};

}  /* namespace */

REGISTER_SERVICE(Iop13xxPmmrStubLow);
REGISTER_SERVICE(Iop13xxPmmrStubMid);
REGISTER_SERVICE(Iop13xxPmmrStubMid2);
REGISTER_SERVICE(Iop13xxPmmrStubHigh);
REGISTER_SERVICE(Iop13xxI2c);
REGISTER_SERVICE(Iop13xxAtuConfig);
