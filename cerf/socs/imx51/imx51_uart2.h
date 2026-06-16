#pragma once

#include "../freescale_uart_impl.h"
#include "../../boards/board_detector.h"

/* MCIMX51RM Table 2-1: UART2 at 0x73FC_0000. Named (not anonymous) so the SYNC2
   VMCU companion peer can resolve it to attach its endpoint + inject RX. */
class Imx51Uart2
    : public cerf_freescale_uart_detail::FreescaleUartBase<0x73FC0000u, 2,
                                                           SocFamily::iMX51> {
public:
    using FreescaleUartBase::FreescaleUartBase;

protected:
    void AssertRxIrq()   override;
    void DeassertRxIrq() override;
};
