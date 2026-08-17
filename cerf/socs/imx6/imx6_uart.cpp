#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"

#include <cstdint>

#include "../../socs/freescale_uart_impl.h"
#include "imx6_uart2.h"

namespace {

/* IMX6SDLRM memory map: UART1 at 0x0202_0000. The i.MX6 UART register layout is
   the same Freescale block already shared by i.MX31/i.MX51. */
class Imx6Uart1
    : public cerf_freescale_uart_detail::FreescaleUartBase<
          0x02020000u, 1, SocFamily::iMX6> {
    using FreescaleUartBase::FreescaleUartBase;
};

}  /* namespace */

REGISTER_SERVICE(Imx6Uart1);
REGISTER_SERVICE(Imx6Uart2);
