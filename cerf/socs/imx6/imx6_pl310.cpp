#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "imx6_mmio_lane.h"

namespace {
class Imx6Pl310 final : public Peripheral {
public:
    using Peripheral::Peripheral;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        /* ARM DDI 0246F Table 3-2: reg1_aux_control resets to 0x02020000. The tag and data RAM
           latency resets in the same table are implementation defined ("0x00000nnn"), and neither
           the ARM TRM nor IMX6DQRM fixes them for this die, so they are left at the array default. */
        regs_[0x104u >> 2] = 0x02020000u;
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
    }
    uint32_t MmioBase() const override { return 0x00A02000u; }
    uint32_t MmioSize() const override { return 0x1000u; }
    uint8_t ReadByte(uint32_t address) override {
        return Imx6ReadMmioByte(address, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t address) override {
        return Imx6ReadMmioHalf(address, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t address) override {
        const uint32_t offset = address - MmioBase();
        if (offset == 0u) return 0x410000C8u;
        if (offset == 4u) return 0x1C100100u;
        if (IsReadRegister(offset)) return regs_[offset >> 2];
        HaltUnsupportedAccess("imx6-pl310 read32 unmodelled register", address, 0);
    }
    void WriteByte(uint32_t address, uint8_t value) override {
        Imx6ForEachMmioLane(address, value, 1u,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteHalf(uint32_t address, uint16_t value) override {
        Imx6ForEachMmioLane(address, value, 2u,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteWord(uint32_t address, uint32_t value) override {
        const uint32_t offset = address - MmioBase();
        if (!IsWriteRegister(offset)) HaltUnsupportedAccess("imx6-pl310 write32 unmodelled register", address, value);
        if (offset == 0x100u) value &= 1u;
        if (IsMaintenanceOperation(offset)) {
            /* ARM DDI 0246F sections 3.1.1 and 3.3.10 define atomic Cache Sync and
               by-Way maintenance with selected Way bits clearing on completion. */
            regs_[offset >> 2] = 0u;
        } else if (IsReadRegister(offset)) {
            regs_[offset >> 2] = value;
        }
    }
    void SaveState(StateWriter& w) override { w.WriteBytes(regs_, sizeof(regs_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes(regs_, sizeof(regs_)); }

private:
    static bool IsMaintenanceOperation(uint32_t offset) {
        switch (offset) {
        case 0x730u:
        case 0x770u:
        case 0x77Cu:
        case 0x7B0u:
        case 0x7BCu:
        case 0x7F0u:
        case 0x7FCu: return true;
        default: return false;
        }
    }

    static bool IsReadRegister(uint32_t offset) {
        switch (offset) {
        case 0x100u:
        case 0x104u:
        case 0xF60u: return true;
        default: return IsMaintenanceOperation(offset) && offset != 0x770u;
        }
    }

    static bool IsWriteRegister(uint32_t offset) {
        switch (offset) {
        case 0x108u:
        case 0x10Cu:
        case 0x220u: return true;
        default: return IsReadRegister(offset) || offset == 0x770u;
        }
    }

    void WriteLane(const Imx6MmioLane& lane) {
        const uint32_t offset = lane.address - MmioBase();
        if (!IsWriteRegister(offset))
            HaltUnsupportedAccess("imx6-pl310 write lane unmodelled register",
                                  lane.address, lane.value);
        if (IsMaintenanceOperation(offset)) {
            regs_[offset >> 2] = 0u;
        } else {
            WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
        }
    }
    uint32_t regs_[0x1000u / 4u]{};
};
REGISTER_SERVICE(Imx6Pl310);
}
