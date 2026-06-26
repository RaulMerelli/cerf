#include "imx51_uart1.h"

#include "../../core/cerf_emulator.h"
#include "../irq_controller.h"

/* UART1 RX interrupt = TZIC source 31 (MCIMX51RM Table 3-2, ARM Domain
   Interrupt Summary: vector 31 = "UART-1 ORed interrupt"; 32 = UART-2,
   33 = UART-3). The BCM4325 BT companion injects its HCI replies into UART1's
   RxFIFO; csp_serial's interrupt-driven read needs this line to wake. */
void Imx51Uart1::AssertRxIrq()   { emu_.Get<IrqController>().AssertIrq(31); }
void Imx51Uart1::DeassertRxIrq() { emu_.Get<IrqController>().DeAssertIrq(31); }

REGISTER_SERVICE(Imx51Uart1);
