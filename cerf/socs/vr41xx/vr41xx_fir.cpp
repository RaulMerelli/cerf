#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"

#include <cstdint>

namespace {

/* VR41xx FIR (Fast IrDA Interface Unit), Internal I/O Space 1 0x0C000040-0x0C000075
   (VR4121 UM Table 27-1 p588 == VR4102 UM Table 26-1 p498: FRSTR 0x40 .. RXFL 0x74). Casio
   serial.dll HWInit sub_1331FE0 reaches only IRSR1 (RMW-clears IRDA_EN). */
constexpr uint32_t kBase = 0x0C000040u;
constexpr uint32_t kSize = 0x36u;      /* 0x0C000040-0x0C000075 (RXFL 0x74 is 16-bit) */
constexpr uint32_t kOffIrsr1 = 0x18u;  /* IRSR1 0x0C000058 */

class Vr41xxFir : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const SocFamily soc = bd->GetSoc();
        return soc == SocFamily::VR4102 || soc == SocFamily::VR4121;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        /* IRSR1 RTCRST=0 / after-reset=0 (VR4121 UM 27.2.8 p598, VR4102 UM 26.2.8 p507). */
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { irsr1_ = 0; });
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override {
        if (addr - kBase == kOffIrsr1) return irsr1_;
        HaltUnsupportedAccess("FIR ReadHalf", addr, 0);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        /* IRSR1 (VR4121 UM 27.2.8 p598 / VR4102 UM 26.2.8 p507): D7 IRDA_EN, D1 IRDA_MD,
           D0 MIR_MD R/W; D15:8 + D6:2 RFU write-0/read-0. */
        if (addr - kBase == kOffIrsr1) { irsr1_ = value & 0x0083u; return; }
        HaltUnsupportedAccess("FIR WriteHalf", addr, value);
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("FIR ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("FIR ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("FIR WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("FIR WriteWord", addr, v); }

    void SaveState(StateWriter& w) override { w.Write(irsr1_); }
    void RestoreState(StateReader& r) override { r.Read(irsr1_); }

private:
    uint16_t irsr1_ = 0;   /* IRSR1 0x58 (IRDA_EN / IRDA_MD / MIR_MD) */
};

}  /* namespace */

REGISTER_SERVICE(Vr41xxFir);
