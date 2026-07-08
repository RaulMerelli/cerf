#include "vr4102_piu.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace {

/* PIU second MMIO block (0x0B0002A0, UM Table 19-1: coordinate + A/D data
   buffers); a Peripheral covers one contiguous range, so the PIU's two blocks
   register separately and this adapter forwards to the Vr4102Piu owner. */
constexpr uint32_t kBase = 0x0B0002A0u;
constexpr uint32_t kSize = 0x20u;   /* 0x2A0-0x2BF */

class Vr4102Piu2Mmio : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4102;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override { return emu_.Get<Vr4102Piu>().ReadHalf2(addr - kBase); }
    void     WriteHalf(uint32_t addr, uint16_t v) override { emu_.Get<Vr4102Piu>().WriteHalf2(addr - kBase, v); }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("PIU2 ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("PIU2 ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("PIU2 WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("PIU2 WriteWord", addr, v); }
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Piu2Mmio);
