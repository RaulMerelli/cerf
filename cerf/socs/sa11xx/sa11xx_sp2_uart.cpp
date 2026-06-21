#include "sa11xx_uart_base.h"

#include "../../core/cerf_emulator.h"

namespace {

/* SA-1110 §11.10 SP2 UART. IrDA-capable port - on iPaq H3xxx this
   surface is reused by the kernel's exception/init path. */

class Sa11xxSp2Uart : public Sa11xxUartBase {
public:
    using Sa11xxUartBase::Sa11xxUartBase;

    uint32_t MmioBase() const override { return 0x80030000u; }

protected:
    const char* ChannelName() const override { return "UART2"; }
};

}  /* namespace */

REGISTER_SERVICE(Sa11xxSp2Uart);
