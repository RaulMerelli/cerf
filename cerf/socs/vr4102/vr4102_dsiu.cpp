#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

/* VR4102 DSIU (Debug Serial Interface Unit), Internal I/O Space 2 block
   0x0B0001A0-0x0B0001BF (UM Table 5-10 p139). Only PORTREG (0x1A0, UM 22.2.1 p436:
   D3:0 R/W, switch the DSIU pins to GPIO; D15:4 reserved) is driven - the
   MobilePro 700 uses the DSIU pins as GPIO, never the serial engine (0x1A2-0x1BF). */
constexpr uint32_t kBase       = 0x0B0001A0u;
constexpr uint32_t kSize       = 0x20u;      /* 0x0B0001A0-0x0B0001BF */
constexpr uint32_t kOffPortReg = 0x00u;      /* PORTREG (UM 22.2.1) */
constexpr uint16_t kPortMask   = 0x000Fu;    /* D3:0 R/W; D15:4 reserved write-0/read-0 */

class Vr4102Dsiu : public Peripheral {
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
        if (addr - kBase == kOffPortReg) return portreg_;
        HaltUnsupportedAccess("DSIU ReadHalf", addr, 0);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        if (addr - kBase == kOffPortReg) { portreg_ = value & kPortMask; return; }
        HaltUnsupportedAccess("DSIU WriteHalf", addr, value);
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("DSIU ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("DSIU ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("DSIU WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("DSIU WriteWord", addr, v); }

    void SaveState(StateWriter& w) override { w.Write(portreg_); }
    void RestoreState(StateReader& r) override { r.Read(portreg_); }

private:
    uint16_t portreg_ = 0;   /* PORTREG, reset 0 (RTCRST + other resets) */
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Dsiu);
