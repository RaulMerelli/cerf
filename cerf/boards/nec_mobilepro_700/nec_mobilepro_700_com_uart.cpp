#include "../../peripherals/uart16550/uart16550.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../board_context.h"

#include <cstdint>

namespace {

/* MobilePro 700 serial port: off-chip 16550 in the VR4102 ISA-IO window,
   registers at reg-index*2 on the 16-bit bus (PA 0x1600FFC0). */
class Vr4102ComUart : public Uart16550 {
public:
    using Uart16550::Uart16550;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::NecMobilePro700;
    }

    uint32_t MmioBase() const override { return 0x1600FFC0u; }
    uint32_t MmioSize() const override { return 0x10u; }

protected:
    uint32_t    RegStride() const override { return 2u; }
    const char* Name()      const override { return "COM"; }

    void SetInterruptLine(bool pending) override {
        if (!pending) return;
        LOG(Caution, "COM UART raised an interrupt; interrupt routing to the ICU "
                     "is not implemented\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
};

}  /* namespace */

REGISTER_SERVICE(Vr4102ComUart);
