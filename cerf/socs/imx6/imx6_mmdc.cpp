#include "../../boards/board_context.h"
#include "../../boards/page_table_builder.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx6_mmio_lane.h"

#include <cstdint>
#include "imx6_id.h"

namespace {

/* IMX6DQRM Rev.2 Table 2-3: MMDC port 0 occupies 0x021B_0000..0x021B_3FFF,
   port 1 0x021B_4000..0x021B_7FFF. */
constexpr uint32_t kMmdcBase = 0x021B0000u;
constexpr uint32_t kMmdcSize = 0x00004000u;

/* IMX6DQRM Rev.2 §44.12.15: MDASP is at +0x40; CS0_END is bits 6:0. */
constexpr uint32_t kOffMdasp = 0x40u;
/* IMX6DQRM Rev.2 §44.12.16: MMDCx_MAARCR is at base + 0x400 and resets to 0x514201F0. */
constexpr uint32_t kOffMaarcr = 0x400u;
constexpr uint32_t kMaarcrReset = 0x514201F0u;

/* IMX6DQRM Rev.2 §44.4.4.2: CS0_END is compared with ADDR[31:25]. */
constexpr uint32_t kCsDecodeUnit = 0x02000000u;

class Imx6Mmdc final : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx6;
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kMmdcBase; }
    uint32_t MmioSize() const override { return kMmdcSize; }

    uint8_t ReadByte(uint32_t a) override {
        return Imx6ReadMmioByte(a, [this](uint32_t address) { return ReadWord(address); });
    }
    uint16_t ReadHalf(uint32_t a) override {
        return Imx6ReadMmioHalf(a, [this](uint32_t address) { return ReadWord(address); });
    }
    uint32_t ReadWord(uint32_t a) override {
        const uint32_t off = a - kMmdcBase;
        if (off == kOffMdasp) return Cs0End();
        HaltUnsupportedAccess("imx6-mmdc read32 unmodelled register", a, 0);
    }

    void WriteByte(uint32_t a, uint8_t v) override {
        HaltUnsupportedAccess("imx6-mmdc write8 unmodelled register", a, v);
    }
    void WriteHalf(uint32_t a, uint16_t v) override {
        HaltUnsupportedAccess("imx6-mmdc write16 unmodelled register", a, v);
    }
    void WriteWord(uint32_t a, uint32_t v) override {
        const uint32_t off = a - kMmdcBase;
        if (off == kOffMaarcr && v == kMaarcrReset) return;
        HaltUnsupportedAccess("imx6-mmdc write32 unmodelled register", a, v);
    }

private:
    uint32_t Cs0SizeBytes() const {
        const PageTableBuilder& builder = emu_.Get<PageTableBuilder>();
        const uint64_t bytes = builder.DramChipSelectBytes();
        if (bytes == 0u || bytes % kCsDecodeUnit != 0u)
            emu_.Get<Fatal>().Die("i.MX6 MMDC: populated DDR 0x%llX is not a whole number of CS0_END units",
                                  static_cast<unsigned long long>(bytes));
        return static_cast<uint32_t>(bytes);
    }

    /* IMX6DQRM Rev.2 §44.12.15: DDR3 and one-channel LPDDR2 use
       DDR_CS_SIZE/32MB + 0x7 because the DDR window starts at 0x10000000. */
    uint32_t Cs0End() const {
        return (Cs0SizeBytes() / kCsDecodeUnit + 0x7u) & 0x7Fu;
    }
};

}

REGISTER_SERVICE(Imx6Mmdc);
