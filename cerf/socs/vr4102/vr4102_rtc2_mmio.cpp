#include "vr4102_rtc.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace {

/* RTC second MMIO block (0x0B0001C0, UM Table 16-1: TCLK + RTCINTREG); a
   Peripheral covers one contiguous range, so the RTC's two blocks register
   separately and this adapter forwards to the Vr4102Rtc owner. */
constexpr uint32_t kBase = 0x0B0001C0u;
constexpr uint32_t kSize = 0x20u;   /* 0x1C0-0x1DF */

class Vr4102Rtc2Mmio : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4102;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override { return emu_.Get<Vr4102Rtc>().ReadHalf2(addr - kBase); }
    void     WriteHalf(uint32_t addr, uint16_t v) override { emu_.Get<Vr4102Rtc>().WriteHalf2(addr - kBase, v); }
    uint32_t ReadWord(uint32_t addr) override { return emu_.Get<Vr4102Rtc>().ReadWord2(addr - kBase); }
    void     WriteWord(uint32_t addr, uint32_t v) override { emu_.Get<Vr4102Rtc>().WriteWord2(addr - kBase, v); }

    uint8_t  ReadByte(uint32_t addr) override { HaltUnsupportedAccess("RTC2 ReadByte", addr, 0); }
    void     WriteByte(uint32_t addr, uint8_t v) override { HaltUnsupportedAccess("RTC2 WriteByte", addr, v); }
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Rtc2Mmio);
