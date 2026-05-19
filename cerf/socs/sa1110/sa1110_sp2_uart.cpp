#include "sa1110_uart_base.h"

#include "../../core/cerf_emulator.h"

namespace {

/* SA-1110 §11.10 SP2 UART. IrDA-capable port — on iPaq H3xxx this
   surface is reused by the kernel's exception/init path. */

class Sa1110Sp2Uart : public Sa1110UartBase {
public:
    using Sa1110UartBase::Sa1110UartBase;

    uint32_t MmioBase() const override { return 0x80030000u; }

protected:
    const char* ChannelName() const override { return "UART2"; }
};

}  /* namespace */

REGISTER_SERVICE(Sa1110Sp2Uart);
