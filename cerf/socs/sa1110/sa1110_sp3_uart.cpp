#include "sa1110_uart_base.h"

#include "../../core/cerf_emulator.h"

namespace {

/* SA-1110 §11.11 SP3 UART. On iPaq H3xxx this is the cradle / debug
   serial; the kernel's exception handler writes here. */

class Sa1110Sp3Uart : public Sa1110UartBase {
public:
    using Sa1110UartBase::Sa1110UartBase;

    uint32_t MmioBase() const override { return 0x80050000u; }

protected:
    const char* ChannelName() const override { return "UART3"; }
};

}  /* namespace */

REGISTER_SERVICE(Sa1110Sp3Uart);
