#include "../../boards/board_context.h"
#include "../../boot/guest_cold_boot.h"
#include "../../core/cerf_emulator.h"
#include "../../core/service.h"
#include "../../cpu/emulated_memory.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace {

/* IMX6DQRM Rev.2 §8.4.1 Figure 8-3: the internal ROM occupies
   0x00000000..0x00017FFF. */
class Imx6BootRom final : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetSoc() == SocFamily::iMX6;
    }

    void OnReady() override {
        SeedVersion();
        emu_.Get<GuestColdBoot>().RegisterReplay([this] { SeedVersion(); });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }
    void PostRestore() override { SeedVersion(); }

    uint32_t MmioBase() const override { return 0x00000000u; }
    uint32_t MmioSize() const override { return 0x00018000u; }

    uint8_t ReadByte(uint32_t addr) override { return emu_.Get<EmulatedMemory>().ReadByte(addr); }
    uint16_t ReadHalf(uint32_t addr) override { return emu_.Get<EmulatedMemory>().ReadHalf(addr); }
    uint32_t ReadWord(uint32_t addr) override { return emu_.Get<EmulatedMemory>().ReadWord(addr); }

    void WriteByte(uint32_t addr, uint8_t value) override {
        HaltUnsupportedAccess("i.MX6 mask-ROM write8", addr, value);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        HaltUnsupportedAccess("i.MX6 mask-ROM write16", addr, value);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        HaltUnsupportedAccess("i.MX6 mask-ROM write32", addr, value);
    }

private:
    /* IMX6DQRM Rev.2 §8.4.1 Figure 8-3: "ROM Version and Copyright Information"
       at 0x00000048. hmi_ktp700_mobile_v13 nk.exe OEMInit sub_8030E69C rejects
       below 0x15. */
    void SeedVersion() {
        constexpr uint32_t kRomVersionGateStub = 0x00000015u;
        emu_.Get<EmulatedMemory>().CopyIn(0x00000048u, &kRomVersionGateStub, sizeof(kRomVersionGateStub));
    }
};

}

REGISTER_SERVICE(Imx6BootRom);
