#include "../freescale_uart_impl.h"

namespace {

/* MCIMX51RM Table 2-2: UART3 at 0x7000_C000 (SPBA0). SBOOT brings it up as its
   serial console (the UCR2.SRST-poll init at Bootloader.bin 0x8FF061EC). */
class Imx51Uart3
    : public cerf_freescale_uart_detail::FreescaleUartBase<0x7000C000u, 3,
                                                           SocFamily::iMX51> {
    using FreescaleUartBase::FreescaleUartBase;
};

}  /* namespace */

REGISTER_SERVICE(Imx51Uart3);
