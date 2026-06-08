#pragma once

#include "sa11xx_uart_base.h"

/* iPaq H3xxx wires MicroP via SP1; MicroP service installs a TX
   listener and pushes RX bytes through Sa11xxUartBase's hooks. */

class Sa11xxSp1Uart : public Sa11xxUartBase {
public:
    using Sa11xxUartBase::Sa11xxUartBase;

    uint32_t MmioBase() const override { return 0x80010000u; }

protected:
    const char* ChannelName()  const override { return "UART1"; }
    /* Linux arch/arm/mach-sa1100 irqs.h: IRQ_Ser1UART maps to
       SA-1110 INTC source bit 15. h3xxx.c routes MicroP IRQ here. */
    int         IntcSourceBit() const override { return 15; }
};
