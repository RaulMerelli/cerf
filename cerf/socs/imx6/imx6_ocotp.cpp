#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx6_mmio_lane.h"
#include <cstdint>

namespace {

class Imx6Ocotp : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return 0x021BC000u; }
    uint32_t MmioSize() const override { return 0x1000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off < MmioSize() && (off & 3u) == 0 && !IsImplementedRegister(off)) {
            return 0u;
        }
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override { Imx6MergeMmioWrite(*this, addr, value, 1u); }
    void WriteHalf(uint32_t addr, uint16_t value) override { Imx6MergeMmioWrite(*this, addr, value, 2u); }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off < MmioSize() && (off & 3u) == 0 && !IsImplementedRegister(off)) {
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

private:
    static bool IsImplementedRegister(uint32_t off) {
        switch (off) {
        case 0x000u: case 0x004u: case 0x008u: case 0x00Cu:
        case 0x010u: case 0x020u: case 0x030u: case 0x040u:
        case 0x050u: case 0x060u: case 0x064u: case 0x068u:
        case 0x06Cu: case 0x090u:
        case 0x660u: case 0x670u: case 0x6D0u: case 0x6E0u:
        case 0x6F0u:
            return true;
        default:
            break;
        }
        return (off >= 0x400u && off <= 0x4F0u && (off & 0xFu) == 0u) ||
               (off >= 0x580u && off <= 0x630u && (off & 0xFu) == 0u);
    }

};
}

REGISTER_SERVICE(Imx6Ocotp);
