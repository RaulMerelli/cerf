#include "../../peripherals/uart16550/uart16550.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"

/* Intel IOP13xx on-chip 16550-compatible UART.
   Base PA 0xFFD82340 (inside the PMMR window 0xFFD80000+). Word register
   stride, observed by siemens_mp377_v1040 nk.exe startup at 0x00409FDC reading LSR at base+0x14
   ((base+0x14) - base = 0x14 = 5 * 4-byte stride, slot 5 = LSR). The Uart16550
   core returns LSR = 0x60 (THRE | TEMT), which is what the busy-wait spins on.

   IRQ source bit: not yet reverse-engineered. During the OAL early-print path
   the driver leaves IER = 0 and never asserts the interrupt line, so a
   not-pending edge is a no-op. We FATAL on a pending edge so the first guest
   write that enables IRQs surfaces the PC of the IER setter, which lets us
   decompile the real source bit before completing this peripheral model. */

namespace {

class Iop13xxUart : public Uart16550 {
public:
    explicit Iop13xxUart(CerfEmulator& emu) : Uart16550(emu, Config{}) {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::IOP13xx;
    }

    uint32_t MmioBase() const override { return 0xFFD82340u; }
    /* 12 16550 register slots × 4-byte stride = 0x30. The Uart16550 default
       0x1000 would swallow neighbouring PMMR blocks (the DMA1 controller
       at PMMR + 0x2480 starts nearby) and route their writes through the
       UART's WriteExtReg fatal path. */
    uint32_t MmioSize() const override { return 0x00000030u; }

protected:
    uint32_t RegStride() const override { return 4u; }
    const char* Name() const override { return "UART0"; }

    void SetInterruptLine(bool pending) override {
        if (!pending) return; /* startup path keeps IER = 0; nothing to route. */
        HaltUnsupportedAccess("SetInterruptLine(pending=true)", MmioBase(), 0);
    }
};

} /* namespace */

REGISTER_SERVICE(Iop13xxUart);
