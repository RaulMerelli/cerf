#include "../freescale_uart_impl.h"

namespace {

/* MCIMX51RM Table 2-2: UART1 at 0x73FB_C000. csp_serial.dll drives it as a serial
   port (USR1 write @0x73FBC094, pc 0xC0D520A8). */
class Imx51Uart1
    : public cerf_freescale_uart_detail::FreescaleUartBase<0x73FBC000u, 1,
                                                           SocFamily::iMX51> {
    using FreescaleUartBase::FreescaleUartBase;
};

}  /* namespace */

REGISTER_SERVICE(Imx51Uart1);
