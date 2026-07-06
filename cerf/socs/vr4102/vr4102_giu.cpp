#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

/* VR4102 GIU (GPIO), 0x0B000100-0x0B00011F (UM Table 5-10 / Table 18-2). Only
   GIUIOSELL (direction; IOS[15]/DCD# fixed input -> masked; UM 18.2.1) is modeled.
   The interrupt block (STAT/EN/TYP/ALSEL/HTSEL) is write-1-to-clear and ICU-driven,
   so those registers FATAL until modeled - storing them as plain R/W is wrong. */
constexpr uint32_t kBase = 0x0B000100u;
constexpr uint32_t kSize = 0x20u;
constexpr uint32_t kOffIoSelL = 0x00u;

class Vr4102Giu : public Peripheral {
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
        if (addr - kBase == kOffIoSelL) return iosel_l_;
        HaltUnsupportedAccess("GIU ReadHalf", addr, 0);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        if (addr - kBase == kOffIoSelL) { iosel_l_ = value & 0x7FFFu; return; }  /* IOS[15] fixed input */
        HaltUnsupportedAccess("GIU WriteHalf", addr, value);
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("GIU ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("GIU ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("GIU WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("GIU WriteWord", addr, v); }

    void SaveState(StateWriter& w) override { w.Write(iosel_l_); }
    void RestoreState(StateReader& r) override { r.Read(iosel_l_); }

private:
    uint16_t iosel_l_ = 0;   /* GIUIOSELL (0x0B000100) */
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Giu);
