#include "imx6_i2c.h"

namespace {
class Imx6I2c2 final : public Imx6I2c<0x021A4000u> {
public:
    using Imx6I2c::Imx6I2c;

};
} // namespace
REGISTER_SERVICE(Imx6I2c2);
