#include "vr4102_icu.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace {

/* VR4102 ICU second MMIO block (0x0B000200, UM Table 14-1); a Peripheral covers
   one contiguous range, so the ICU's two blocks register separately. */
constexpr uint32_t kBase = 0x0B000200u;
constexpr uint32_t kSize = 0x10u;

class Vr4102Icu2Mmio : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4102;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override {
        return emu_.Get<Vr4102Icu>().ReadHalf2(addr - kBase);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        emu_.Get<Vr4102Icu>().WriteHalf2(addr - kBase, value);
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("ICU2 ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("ICU2 ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("ICU2 WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("ICU2 WriteWord", addr, v); }
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Icu2Mmio);
