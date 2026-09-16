#include "imx6_pwm.h"
namespace {
class Imx6Pwm2 final : public Imx6Pwm<0x02084000u> {
public:
    using Imx6Pwm::Imx6Pwm;

};
} // namespace
REGISTER_SERVICE(Imx6Pwm2);
