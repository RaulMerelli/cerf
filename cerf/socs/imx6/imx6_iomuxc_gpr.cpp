#include "../../core/cerf_emulator.h"
#include "../../state/state_stream.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../core/service.h"
#include "imx6_mmio_lane.h"

#include <algorithm>
#include <iterator>

namespace {

class Imx6IomuxcGpr : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        ResetRegisters();
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
    }

    uint32_t MmioBase() const override { return 0x020E0000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        uint32_t reset = 0u;
        if (ResetValue(off, reset)) return regs_[off >> 2];
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override { Imx6MergeMmioWrite(*this, addr, value, 1u); }
    void WriteHalf(uint32_t addr, uint16_t value) override { Imx6MergeMmioWrite(*this, addr, value, 2u); }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        uint32_t reset = 0u;
        if (ResetValue(off, reset)) {
            regs_[off >> 2] = value;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

    void SaveState(StateWriter& w) override { w.WriteBytes(regs_, sizeof(regs_)); }

    void RestoreState(StateReader& r) override { r.ReadBytes(regs_, sizeof(regs_)); }

private:
    /* IMX6SDLRM Rev.4 §37.4, IOMUXC memory map. */
    static bool ResetValue(uint32_t off, uint32_t& value) {
        if ((off & 3u) != 0u) return false;
        switch (off) {
        case 0x004u: value = 0x48400005u; return true;
        case 0x00Cu: value = 0x01E00000u; return true;
        case 0x018u:
        case 0x01Cu: value = 0x22222222u; return true;
        case 0x028u: value = 0x00003800u; return true;
        case 0x030u: value = 0x0F000000u; return true;
        case 0x034u: value = 0x00000008u; return true;
        default: break;
        }
        if (off <= 0x034u) { value = 0u; return true; }
        if (off >= 0x04Cu && off <= 0x10Cu) { value = 0x00000005u; return true; }
        if (off >= 0x110u && off <= 0x140u) { value = 0u; return true; }
        if (off >= 0x144u && off <= 0x180u) { value = 0x00000005u; return true; }
        if (off >= 0x184u && off <= 0x1C8u) { value = 0u; return true; }
        if (off >= 0x1CCu && off <= 0x1D0u) { value = 0x00000005u; return true; }
        if (off >= 0x1D4u && off <= 0x1E0u) { value = 0u; return true; }
        if (off >= 0x1E4u && off <= 0x35Cu) { value = 0x00000005u; return true; }
        if (off >= 0x360u && off <= 0x420u) { value = 0x0001B0B0u; return true; }
        if (off >= 0x424u && off <= 0x460u) { value = 0x00008000u; return true; }
        if (off == 0x464u) { value = 0x00008030u; return true; }
        if (off >= 0x468u && off <= 0x46Cu) { value = 0x00008000u; return true; }
        if (off >= 0x470u && off <= 0x490u) { value = 0x00008030u; return true; }
        if (off == 0x494u) { value = 0x00083030u; return true; }
        if (off >= 0x498u && off <= 0x49Cu) { value = 0x00008000u; return true; }
        if (off == 0x4A0u) { value = 0x0000B000u; return true; }
        if (off >= 0x4A4u && off <= 0x4A8u) { value = 0x00003000u; return true; }
        if (off >= 0x4ACu && off <= 0x4B0u) { value = 0x00008030u; return true; }
        if (off >= 0x4B4u && off <= 0x4B8u) { value = 0x00003030u; return true; }
        if (off >= 0x4BCu && off <= 0x4D8u) { value = 0x00002030u; return true; }
        if (off == 0x4DCu) { value = 0x00008000u; return true; }
        if (off >= 0x4E0u && off <= 0x510u) { value = 0x0000B0B1u; return true; }
        if (off >= 0x514u && off <= 0x528u) { value = 0x0001B0B0u; return true; }
        if (off == 0x52Cu || off == 0x550u) { value = 0x000130B0u; return true; }
        if (off >= 0x530u && off <= 0x54Cu) { value = 0x0001B0B0u; return true; }
        if (off >= 0x554u && off <= 0x598u) { value = 0x0000B0B1u; return true; }
        if (off >= 0x59Cu && off <= 0x5A0u) { value = 0x0001B0B0u; return true; }
        if (off >= 0x5A4u && off <= 0x5ACu) { value = 0x0000B0B1u; return true; }
        if (off == 0x5B0u || off == 0x614u) { value = 0x0000B060u; return true; }
        if (off >= 0x5B4u && off <= 0x5D8u) { value = 0x0001B0B0u; return true; }
        if (off == 0x5DCu || off == 0x650u) { value = 0x000130B0u; return true; }
        if (off >= 0x5E0u && off <= 0x610u) { value = 0x0001B0B0u; return true; }
        if (off == 0x618u || off == 0x61Cu || off == 0x624u || off == 0x628u) {
            value = 0x00007060u; return true;
        }
        if (off == 0x620u) { value = 0x000090B1u; return true; }
        if (off >= 0x62Cu && off <= 0x64Cu) { value = 0x0001B0B0u; return true; }
        if (off >= 0x654u && off <= 0x690u) { value = 0x0001B0B0u; return true; }
        if (off >= 0x694u && off <= 0x6A0u) { value = 0x0001B030u; return true; }
        if (off >= 0x6A4u && off <= 0x6A8u) { value = 0x00013030u; return true; }
        if (off >= 0x6ACu && off <= 0x6B8u) { value = 0x0001B030u; return true; }
        if (off >= 0x6BCu && off <= 0x6C0u) { value = 0x00013030u; return true; }
        if (off >= 0x6C4u && off <= 0x744u) { value = 0x0001B0B0u; return true; }
        switch (off) {
        case 0x748u: case 0x74Cu: case 0x764u: case 0x76Cu: case 0x770u:
        case 0x778u: case 0x77Cu: case 0x780u: case 0x784u: case 0x78Cu:
            value = 0x00000030u; return true;
        case 0x754u: value = 0x00001000u; return true;
        case 0x758u: value = 0x00002000u; return true;
        case 0x768u: case 0x774u: value = 0x00080000u; return true;
        case 0x750u: case 0x75Cu: case 0x760u: case 0x788u:
            value = 0u; return true;
        default: break;
        }
        if (off >= 0x790u && off <= 0x7CCu) { value = 0u; return true; }
        if (off >= 0x7D4u && off <= 0x8D4u) { value = 0u; return true; }
        if (off >= 0x8DCu && off <= 0x938u) { value = 0u; return true; }
        /* Linux imx6q-pinfunc.h: i.MX6Q UART5, USB OC and SD1 WP inputs. */
        if (off >= 0x93Cu && off <= 0x94Cu) { value = 0u; return true; }
        return false;
    }

    void ResetRegisters() {
        std::fill(std::begin(regs_), std::end(regs_), 0u);
        for (uint32_t off = 0u; off < sizeof(regs_); off += 4u) {
            uint32_t value = 0u;
            if (ResetValue(off, value)) regs_[off >> 2] = value;
        }
    }

    uint32_t regs_[0x950u / 4u]{};
};

}

REGISTER_SERVICE(Imx6IomuxcGpr);
