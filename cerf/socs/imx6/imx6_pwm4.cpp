#include "imx6_pwm.h"
namespace {
class Imx6Pwm4 final : public Imx6Pwm<0x0208C000u> {
public:
    using Imx6Pwm::Imx6Pwm;

};
} // namespace
REGISTER_SERVICE(Imx6Pwm4);
