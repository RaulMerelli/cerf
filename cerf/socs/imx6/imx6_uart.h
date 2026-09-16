#pragma once

#include "../freescale_uart_impl.h"

template <uint32_t kBase, int kUartNum>
class Imx6Uart : public cerf_freescale_uart_detail::FreescaleUartBase<kBase, kUartNum, SocFamily::iMX6> {
public:
    using Parent = cerf_freescale_uart_detail::FreescaleUartBase<kBase, kUartNum, SocFamily::iMX6>;
    using Parent::Parent;

    void OnReady() override { this->emu_.Get<PeripheralDispatcher>().RegisterResettable(this); }
};
