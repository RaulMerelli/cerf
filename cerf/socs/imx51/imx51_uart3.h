#pragma once

#include "../freescale_uart_impl.h"
#include "../../boards/board_context.h"

/* MCIMX51RM Table 2-2: UART3 at 0x7000_C000 (SPBA0). SBOOT brings it up as its
   serial console (the UCR2.SRST-poll init at Bootloader.bin 0x8FF061EC). Named
   (not anonymous) so Imx51Sdma can resolve it to bind its DMA-request events. */
class Imx51Uart3
    : public cerf_freescale_uart_detail::FreescaleUartBase<0x7000C000u, 3,
                                                           SocFamily::iMX51> {
public:
    using FreescaleUartBase::FreescaleUartBase;
};
