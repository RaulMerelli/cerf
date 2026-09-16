#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"

#include <cstdint>

#include "imx6_uart.h"

namespace {

/* IMX6SDLRM memory map: UART1 at 0x0202_0000. The i.MX6 UART register layout is
   the same Freescale block already shared by i.MX31/i.MX51. */
class Imx6Uart1 : public Imx6Uart<0x02020000u, 1> {
    using Imx6Uart::Imx6Uart;
};

}

REGISTER_SERVICE(Imx6Uart1);
