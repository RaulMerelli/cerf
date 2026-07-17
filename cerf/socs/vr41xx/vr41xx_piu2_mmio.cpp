#include "vr41xx_piu.h"

#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace {

/* The PIU's data-buffer window, 0x0B0002A0-0x0B0002BF (VR4121 UM Table 20-1,
   VR4102 UM Table 19-1). */
class Vr41xxPiu2Mmio : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override { return emu_.TryGet<Vr41xxPiu>() != nullptr; }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return emu_.Get<Vr41xxPiu>().Piu2Base(); }
    uint32_t MmioSize() const override { return emu_.Get<Vr41xxPiu>().Piu2Size(); }

    uint16_t ReadHalf(uint32_t addr) override {
        auto& piu = emu_.Get<Vr41xxPiu>();
        return piu.ReadHalf2(addr - piu.Piu2Base());
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        auto& piu = emu_.Get<Vr41xxPiu>();
        piu.WriteHalf2(addr - piu.Piu2Base(), value);
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("VR41xx PIU2 ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("VR41xx PIU2 ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("VR41xx PIU2 WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("VR41xx PIU2 WriteWord", addr, v); }
};

}  /* namespace */

REGISTER_SERVICE(Vr41xxPiu2Mmio);
