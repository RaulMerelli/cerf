#include "../../peripherals/uart16550/uart16550.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../state/state_stream.h"
#include "vr4102_icu.h"

#include <cstdint>

namespace {

/* VR4102 on-chip SIU (Serial Interface Unit): NS16550-compatible UART at
   Internal I/O Space 1 base 0x0C000000, byte-addressed (stride 1; SCR at
   0x0C000007). serial.dll's COM PDD drives it interrupt-driven; the SIU
   interrupt is SYSINT1 SIUINTR, D9 (UM 14.2.1, p295). */
class Vr4102Siu : public Uart16550 {
public:
    using Uart16550::Uart16550;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4102;
    }

    uint32_t MmioBase() const override { return 0x0C000000u; }
    uint32_t MmioSize() const override { return 0x20u; }   /* SIU block; HSP follows at +0x20 */

protected:
    uint32_t    RegStride() const override { return 1u; }
    const char* Name()      const override { return "SIU"; }

    void SetInterruptLine(bool pending) override {
        emu_.Get<Vr4102Icu>().SetSysint1Source(kSiuIntr, pending);
    }

    /* 0x08 SIUIRSEL D5:0 R/W (UM 24.2.13, p478). 0x09 is undocumented (UM ch.24
       ends at 0x08) - grounded from serial.dll's baud setup (sub_158116C), which
       pulses bit 0 as a divisor-reload strobe. */
    uint32_t ReadExtReg(uint32_t idx) override {
        if (idx == 8u) return irsel_;
        if (idx == 9u) return baud_reload_;
        return Uart16550::ReadExtReg(idx);
    }
    void WriteExtReg(uint32_t idx, uint32_t value) override {
        if (idx == 8u) { irsel_ = static_cast<uint8_t>(value & 0x3Fu); return; }
        if (idx == 9u) { baud_reload_ = static_cast<uint8_t>(value & 0xFFu); return; }
        Uart16550::WriteExtReg(idx, value);
    }

    void SaveState(StateWriter& w) override {
        Uart16550::SaveState(w); w.Write(irsel_); w.Write(baud_reload_);
    }
    void RestoreState(StateReader& r) override {
        Uart16550::RestoreState(r); r.Read(irsel_); r.Read(baud_reload_);
    }

private:
    static constexpr uint16_t kSiuIntr = 1u << 9;   /* SYSINT1 D9 SIUINTR (UM 14.2.1, p295) */
    uint8_t irsel_       = 0;   /* SIU 0x08 SIUIRSEL */
    uint8_t baud_reload_ = 0;   /* SIU 0x09 divisor-reload strobe */
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Siu);
