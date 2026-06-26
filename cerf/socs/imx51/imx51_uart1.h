#pragma once

#include "../freescale_uart_impl.h"
#include "../../boards/board_detector.h"

/* MCIMX51RM Table 2-2: UART1 at 0x73FB_C000. Named (not anonymous) so the SYNC2
   Bluetooth (Broadcom BCM4325) HCI companion can resolve it to attach its
   endpoint + inject RX. csp_serial.dll registers it as COM1 (default.hv
   BuiltIn key: Dll=csp_serial.dll, Prefix=COM, Index=1, IoBase=0x73FBC000). */
class Imx51Uart1
    : public cerf_freescale_uart_detail::FreescaleUartBase<0x73FBC000u, 1,
                                                           SocFamily::iMX51> {
public:
    using FreescaleUartBase::FreescaleUartBase;

protected:
    void AssertRxIrq()   override;
    void DeassertRxIrq() override;
};
