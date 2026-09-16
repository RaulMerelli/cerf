#pragma once

#include "imx6_uart.h"
#include "imx6_gic.h"

/* IMX6SDLRM Rev.4 Table 2-3 maps UART2 at 0x021E8000. */
class Imx6Uart2 : public Imx6Uart<0x021E8000u, 2> {
public:
    using Imx6Uart::Imx6Uart;

protected:
    void AssertRxIrq() override { emu_.Get<Imx6Gic>().AssertSpi(27); }
    void DeassertRxIrq() override { emu_.Get<Imx6Gic>().DeAssertSpi(27); }
};
