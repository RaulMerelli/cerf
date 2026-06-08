#include "sa11xx_uart_base.h"

#include "../../core/cerf_emulator.h"

namespace {

/* SA-1110 §11.11 SP3 UART. On iPaq H3xxx this is the cradle / debug
   serial; the kernel's exception handler writes here. */

class Sa11xxSp3Uart : public Sa11xxUartBase {
public:
    using Sa11xxUartBase::Sa11xxUartBase;

    uint32_t MmioBase() const override { return 0x80050000u; }

protected:
    const char* ChannelName() const override { return "UART3"; }
};

}  /* namespace */

REGISTER_SERVICE(Sa11xxSp3Uart);
