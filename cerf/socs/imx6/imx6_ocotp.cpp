#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx6_mmio_lane.h"

#include <array>
#include <cstdint>
#include "imx6_id.h"

namespace {

/* IMX6DQRM Rev.2 Table 2-3: OCOTP_CTRL occupies 0x021BC000..0x021BFFFF. */
constexpr uint32_t kOcotpBase = 0x021BC000u;
constexpr uint32_t kOcotpSize = 0x00004000u;

/* IMX6DQRM Rev.2 §46.5: OCOTP_TIMING resets to 0x01461299, OCOTP_VERSION to
   0x02000000, and every other register in the file resets to 0. */
constexpr uint32_t kOffTiming = 0x010u;
constexpr uint32_t kTimingReset = 0x01461299u;
constexpr uint32_t kOffVersion = 0x090u;
constexpr uint32_t kVersionReset = 0x02000000u;

/* IMX6DQRM Rev.2 §46.1.1 and §46.3.2: 4 Kbit of OTP in 16 banks of 8 words,
   housed in the shadow-register aperture that follows the control file. */
constexpr uint32_t kShadowBase = 0x400u;
constexpr uint32_t kShadowEnd = 0xC00u;

constexpr bool IsControlRegister(uint32_t off) {
    switch (off) {
    case 0x000u: case 0x004u: case 0x008u: case 0x00Cu:
    case 0x010u: case 0x020u: case 0x030u: case 0x040u:
    case 0x050u: case 0x060u: case 0x064u: case 0x068u:
    case 0x06Cu: case 0x090u:
        return true;
    default:
        return false;
    }
}

class Imx6Ocotp final : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx6;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kOcotpBase; }
    uint32_t MmioSize() const override { return kOcotpSize; }

    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }

    /* hmi_ktp700_mobile_v13 nk.exe OEMInit sub_8030E69C maps 0x021BC000 and reads
       the board identity from the shadow aperture at +0x820..+0x83C, +0x878 and
       +0x87C; CERF programs no fuses, so those words hold their reset value. */
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - kOcotpBase;
        if ((off & 3u) == 0u) {
            if (IsControlRegister(off)) return regs_[off / 4u];
            if (off >= kShadowBase && off < kShadowEnd) return regs_[off / 4u];
        }
        HaltUnsupportedAccess("imx6-ocotp read32 unmodelled register", addr, 0);
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        HaltUnsupportedAccess("imx6-ocotp write8", addr, value);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        HaltUnsupportedAccess("imx6-ocotp write16", addr, value);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        HaltUnsupportedAccess("imx6-ocotp write32", addr, value);
    }

private:
    std::array<uint32_t, kShadowEnd / 4u> regs_ = [] {
        std::array<uint32_t, kShadowEnd / 4u> r{};
        r[kOffTiming / 4u] = kTimingReset;
        r[kOffVersion / 4u] = kVersionReset;
        return r;
    }();
};

}

REGISTER_SERVICE(Imx6Ocotp);
