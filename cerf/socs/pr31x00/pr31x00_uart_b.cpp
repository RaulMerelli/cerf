#include "pr31x00_uart.h"

#include "../../core/cerf_emulator.h"

#include <cstdint>

namespace {

class Pr31x00UartB : public Pr31x00Uart {
public:
    using Pr31x00Uart::Pr31x00Uart;

    uint32_t MmioBase() const override { return 0x10C000C8u; }

protected:
    const char* TxSource() const override { return "UARTB"; }
};

}  /* namespace */

REGISTER_SERVICE(Pr31x00UartB);
