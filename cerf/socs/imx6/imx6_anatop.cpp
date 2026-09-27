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
#include "imx6_id.h"

namespace {

constexpr uint32_t kImx6SlDigprogAbsentStub = 0u;
constexpr uint32_t kPllLock = 0x80000000u;
constexpr uint32_t kPllPower = 0x00001000u;

class Imx6Anatop : public Peripheral {
public:
    using Peripheral::Peripheral;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx6;
    }
    void OnReady() override {
        ResetRegisters();
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
    }
    uint32_t MmioBase() const override { return 0x020C8000u; }
    uint32_t MmioSize() const override { return 0x1000u; }
    uint8_t ReadByte(uint32_t addr) override {
        if (((addr - MmioBase()) & ~3u) == 0x280u)
            HaltUnsupportedAccess("read8 i.MX6SL DIGPROG probe", addr, 0);
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        if (((addr - MmioBase()) & ~3u) == 0x280u)
            HaltUnsupportedAccess("read16 i.MX6SL DIGPROG probe", addr, 0);
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        /* NXP AN12263 Rev.0, Table 2 note 1: i.MX6D/Q Rev 1.3 USB_ANALOG_DIGPROG = 0x00630005. */
        if (off == 0x260u) return 0x00630005u;
        /* hmi_ktp400_mobile_v17, nk.exe: sub_80318660 @ VA 0x80318660 probes +0x280 for SL then +0x260.
           Linux arch/arm/mach-imx/anatop.c names +0x280 ANADIG_DIGPROG_IMX6SL; IMX6DQRM Rev.2 §66.4 omits it. */
        if (off == 0x280u) return kImx6SlDigprogAbsentStub;
        if (IsRegister(off)) return regs_[(off & ~0xFu) / 0x10u];
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        Imx6ForEachMmioLane(addr, value, 1u,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        Imx6ForEachMmioLane(addr, value, 2u,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (IsWritableRegister(off)) {
            const uint32_t base = off & ~0xFu;
            uint32_t& reg = regs_[base / 0x10u];
            const uint32_t writable_value = IsPllControl(base) ? value & ~kPllLock : value;
            switch (off & 0xCu) {
            case 0x0: reg = writable_value; break;
            case 0x4: reg |= writable_value; break;
            case 0x8: reg &= ~writable_value; break;
            case 0xC: reg ^= writable_value; break;
            }
            UpdatePllLockAfterWrite(base, reg);
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

    void SaveState(StateWriter& w) override { w.WriteBytes("regs", regs_, sizeof(regs_)); }

    void RestoreState(StateReader& r) override { r.ReadBytes("regs", regs_, sizeof(regs_)); }

private:
    static bool IsPllControl(uint32_t base) {
        switch (base) {
        case 0x000u: case 0x010u: case 0x020u: case 0x030u:
        case 0x070u: case 0x0A0u: case 0x0D0u: case 0x0E0u:
            return true;
        default:
            return false;
        }
    }

    static bool IsPllPowered(uint32_t base, uint32_t reg) {
        switch (base) {
        case 0x010u: case 0x020u:
            return (reg & kPllPower) != 0u;
        case 0x000u: case 0x030u: case 0x070u: case 0x0A0u: case 0x0E0u:
            return (reg & kPllPower) == 0u;
        default:
            return false;
        }
    }

    static void UpdatePllLockAfterWrite(uint32_t base, uint32_t& reg) {
        if (!IsPllControl(base)) return;
        /* IMX6DQRM Rev.2 §§18.5.1.5.3, 18.7.1-18.7.15: LOCK is status; USB1/2 bit12=POWER,
           other powered PLLs bit12=POWERDOWN, and BYPASS selects the reference output. hmi_ktp400_mobile_v13
           cspddk.dll sub_EF5AC3C8 @ 0xEF5AC4F0..0xEF5AC540 clears VIDEO POWERDOWN then polls LOCK. */
        reg &= ~kPllLock;
        if (IsPllPowered(base, reg)) reg |= kPllLock;
    }

    static bool HasAliases(uint32_t base) {
        switch (base) {
        case 0x000u: case 0x010u: case 0x020u: case 0x030u:
        case 0x070u: case 0x0A0u: case 0x0D0u: case 0x0E0u:
        case 0x0F0u: case 0x100u: case 0x150u: case 0x160u:
        case 0x170u: case 0x1A0u: case 0x1B0u: case 0x1F0u:
        case 0x200u: case 0x210u: case 0x250u:
            return true;
        default:
            return false;
        }
    }

    static bool IsSingleRegister(uint32_t off) {
        switch (off) {
        case 0x040u: case 0x050u: case 0x060u: case 0x080u:
        case 0x090u: case 0x0B0u: case 0x0C0u: case 0x1C0u:
        case 0x1D0u: case 0x220u: case 0x230u: case 0x260u:
            return true;
        default:
            return false;
        }
    }

    static bool IsRegister(uint32_t off) {
        if ((off & 3u) != 0u) return false;
        return HasAliases(off & ~0xFu) || IsSingleRegister(off);
    }

    static bool IsWritableRegister(uint32_t off) {
        if (!IsRegister(off)) return false;
        return off != 0x1C0u && off != 0x1D0u && off != 0x220u &&
               off != 0x230u && off != 0x260u;
    }

    void ResetRegisters() {
        std::fill(std::begin(regs_), std::end(regs_), 0u);
        /* IMX6DQRM Rev.2 §18.7 and §66.4. */
        regs_[0x000u / 0x10u] = 0x00013042u;
        regs_[0x010u / 0x10u] = 0x00012000u;
        regs_[0x020u / 0x10u] = 0x00012000u;
        regs_[0x030u / 0x10u] = 0x00013001u;
        regs_[0x060u / 0x10u] = 0x00000012u;
        regs_[0x070u / 0x10u] = 0x00011006u;
        regs_[0x080u / 0x10u] = 0x05F5E100u;
        regs_[0x090u / 0x10u] = 0x2964619Cu;
        regs_[0x0A0u / 0x10u] = 0x0001100Cu;
        regs_[0x0B0u / 0x10u] = 0x05F5E100u;
        regs_[0x0C0u / 0x10u] = 0x10A24447u;
        regs_[0x0D0u / 0x10u] = 0x00010000u;
        regs_[0x0E0u / 0x10u] = 0x00011001u;
        regs_[0x0F0u / 0x10u] = 0x1311100Cu;
        regs_[0x100u / 0x10u] = 0x1018101Bu;
        regs_[0x150u / 0x10u] = 0x04000000u;
        regs_[0x170u / 0x10u] = 0x00272727u;
        regs_[0x1A0u / 0x10u] = 0x00100004u;
        regs_[0x1F0u / 0x10u] = 0x00000002u;
        regs_[0x200u / 0x10u] = 0x00100004u;
        regs_[0x250u / 0x10u] = 0x00000002u;
    }

    void WriteLane(const Imx6MmioLane& lane) {
        const uint32_t off = lane.address - MmioBase();
        if (!IsWritableRegister(off))
            HaltUnsupportedAccess("write lane", lane.address, lane.value);
        const uint32_t base = off & ~0xFu;
        uint32_t& reg = regs_[base / 0x10u];
        const uint32_t write_mask = IsPllControl(base) ? lane.mask & ~kPllLock : lane.mask;
        const uint32_t write_value = lane.value & write_mask;
        switch (off & 0xCu) {
        case 0x0: reg = (reg & ~write_mask) | write_value; break;
        case 0x4: reg |= write_value; break;
        case 0x8: reg &= ~write_value; break;
        case 0xC: reg ^= write_value; break;
        }
        UpdatePllLockAfterWrite(base, reg);
    }
    uint32_t regs_[0x270u / 0x10u]{};
};

}

REGISTER_SERVICE(Imx6Anatop);
