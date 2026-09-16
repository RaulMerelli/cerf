#include "imx6_i2c.h"

namespace {
class Imx6I2c1 final : public Imx6I2c<0x021A0000u> {
public:
    using Imx6I2c::Imx6I2c;

};
} // namespace
REGISTER_SERVICE(Imx6I2c1);
