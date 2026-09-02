#pragma once

#include "../freescale_uart_impl.h"
#include "imx6_gic.h"

/* i.MX6 UART2 in the AIPS2 aperture at 0x021E8000 (same Freescale block shared
   by i.MX31/i.MX51). Named (not anonymous) so a board's off-chip serial
   companion (the KTP400 ConnBox/MicroOMS peer) can resolve it to attach its
   endpoint and inject RX. */
class Imx6Uart2 : public cerf_freescale_uart_detail::FreescaleUartBase<0x021E8000u, 2, SocFamily::iMX6> {
public:
    using FreescaleUartBase::FreescaleUartBase;

protected:
    /* i.MX6QDL UART2 interrupt: GIC SPI 27 (imx6qdl.dtsi uart2@21e8000).
       The KTP ConnBox driver polls URXD while identifying the box, then uses
       interrupt-driven ReadFile for the framed MicroOMS exchange. */
    void AssertRxIrq() override { emu_.Get<Imx6Gic>().AssertSpi(27); }
    void DeassertRxIrq() override { emu_.Get<Imx6Gic>().DeAssertSpi(27); }
};
