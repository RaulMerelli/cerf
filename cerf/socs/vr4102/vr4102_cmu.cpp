#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

/* VR4102 CMU (Clock Mask Unit), Internal I/O Space 2 (UM Table 13-1): the single
   CMUCLKMSK register gates TClock / 18.432-MHz / 48-MHz to the KIU/PIU/AIU/SIU/
   DSIU/FIR/HSP units (1=supply, 0=mask). Reset masks every clock. */
constexpr uint32_t kBase   = 0x0B000060u;
constexpr uint32_t kSize   = 0x20u;      /* 0x0B000060-0x0B00007F */
constexpr uint32_t kOffMsk = 0x00u;      /* CMUCLKMSK 0x0B000060 */
constexpr uint16_t kMask   = 0x073Fu;    /* writable D10/9/8 + D5-0; D15-11/7/6 reserved (UM 13.2.1) */

class Vr4102Cmu : public Peripheral {
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
        if (addr - kBase == kOffMsk) return clkmsk_;
        HaltUnsupportedAccess("CMU ReadHalf", addr, 0);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        if (addr - kBase == kOffMsk) { clkmsk_ = value & kMask; return; }
        HaltUnsupportedAccess("CMU WriteHalf", addr, value);
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("CMU ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("CMU ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("CMU WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("CMU WriteWord", addr, v); }

    void SaveState(StateWriter& w) override { w.Write(clkmsk_); }
    void RestoreState(StateReader& r) override { r.Read(clkmsk_); }

private:
    uint16_t clkmsk_ = 0;   /* CMUCLKMSK, reset = all clocks masked */
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Cmu);
