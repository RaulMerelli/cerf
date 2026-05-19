#pragma once

#include "sa1110_uart_base.h"

/* iPaq H3xxx wires MicroP via SP1; MicroP service installs a TX
   listener and pushes RX bytes through Sa1110UartBase's hooks. */

class Sa1110Sp1Uart : public Sa1110UartBase {
public:
    using Sa1110UartBase::Sa1110UartBase;

    uint32_t MmioBase() const override { return 0x80010000u; }

protected:
    const char* ChannelName()  const override { return "UART1"; }
    /* Linux arch/arm/mach-sa1100 irqs.h: IRQ_Ser1UART maps to
       SA-1110 INTC source bit 15. h3xxx.c routes MicroP IRQ here. */
    int         IntcSourceBit() const override { return 15; }
};
