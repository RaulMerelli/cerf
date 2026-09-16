#include "../../boards/board_context.h"
#include "../../boards/page_table_builder.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "imx6_mmio_lane.h"

#include <cstdint>

namespace {

/* IMX6SDLRM Rev. 4 § 45 "Multi Mode DDR Controller (MMDC)": the controller
   occupies 0x021B_0000..0x021B_3FFF, with the second channel at 0x021B_4000. */
constexpr uint32_t kMmdcBase = 0x021B0000u;
constexpr uint32_t kMmdcSize = 0x00004000u;

/* IMX6SDLRM Rev.4 §45.12.15: MDASP is at +0x40; CS0_END is bits 6:0. */
constexpr uint32_t kOffMdasp = 0x40u;
/* i.MX 6Solo/6DualLite Reference Manual Rev.4 §45.12.16: MMDCx_MAARCR is
   at base + 0x400 and resets to 0x514201F0. */
constexpr uint32_t kOffMaarcr = 0x400u;
constexpr uint32_t kMaarcrReset = 0x514201F0u;

class Imx6Mmdc final : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }

    void OnReady() override {
        mdasp_ = Mdasp();
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
    }

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
        if (off == kOffMdasp) return mdasp_;
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

    void SaveState(StateWriter& w) override { w.Write(mdasp_); }
    void RestoreState(StateReader& r) override { r.Read(mdasp_); }

private:
    uint32_t Mdasp() const {
        uint64_t bytes = 0;
        for (const auto& r : emu_.Get<PageTableBuilder>().CachedDramRegions())
            bytes += r.size;
        /* IMX6SDLRM Rev.4 §45.12.15: DDR3 and one-channel LPDDR2 use
           DDR_CS_SIZE/32MB + 0x7 because the DDR window starts at 0x10000000. */
        const uint32_t cs_end = static_cast<uint32_t>(bytes / (32u * 1024u * 1024u)) + 0x7u;
        return cs_end & 0x7Fu;
    }

    uint32_t mdasp_ = 0x0000003Fu;
};

}

REGISTER_SERVICE(Imx6Mmdc);
