#include "imx51_uart2.h"

#include "../../core/cerf_emulator.h"
#include "../irq_controller.h"

/* UART2 RX interrupt = TZIC source 32 (MCIMX51RM Table 3-2, ARM Domain
   Interrupt Summary). */
void Imx51Uart2::AssertRxIrq()   { emu_.Get<IrqController>().AssertIrq(32); }
void Imx51Uart2::DeassertRxIrq() { emu_.Get<IrqController>().DeAssertIrq(32); }

REGISTER_SERVICE(Imx51Uart2);
