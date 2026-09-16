#include "imx6_gpio.h"

namespace {

class Imx6Gpio2 final : public Imx6Gpio<0x020A0000u> {
public:
    using Imx6Gpio::Imx6Gpio;

};

} // namespace

REGISTER_SERVICE(Imx6Gpio2);
