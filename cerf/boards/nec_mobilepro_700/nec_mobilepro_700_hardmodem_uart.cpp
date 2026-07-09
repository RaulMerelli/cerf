#include "../../peripherals/uart16550/uart16550.h"

#include "../../core/cerf_emulator.h"
#include "../board_context.h"
#include "../../socs/vr4102/vr4102_giu.h"

#include <cstdint>

namespace {

/* MobilePro 700 built-in modem: off-chip stride-1 16550 at ISA-IO 0x17100000,
   driven by serial.dll's "Drivers\BuiltIn\HardModem" PDD (window map sub_1587374). */
class Vr4102HardModemUart : public Uart16550 {
public:
    using Uart16550::Uart16550;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::NecMobilePro700;
    }

    uint32_t MmioBase() const override { return 0x17100000u; }
    uint32_t MmioSize() const override { return 0x08u; }   /* 8 regs, stride 1 */

protected:
    uint32_t    RegStride() const override { return 1u; }
    const char* Name()      const override { return "HardModem"; }

    void SetInterruptLine(bool pending) override {
        /* GIU pin 0: nk.exe OAL ISR sub_9F002050 returns SYSINTR 39 on GIUINT bit0. */
        emu_.Get<Vr4102Giu>().SetPinLevel(0, pending);
    }
};

}  /* namespace */

REGISTER_SERVICE(Vr4102HardModemUart);
