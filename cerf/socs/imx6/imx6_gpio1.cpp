#include "imx6_gpio.h"

namespace {

class Imx6Gpio1 final : public Imx6Gpio<0x0209C000u> {
public:
    using Imx6Gpio::Imx6Gpio;

};

} // namespace

REGISTER_SERVICE(Imx6Gpio1);
