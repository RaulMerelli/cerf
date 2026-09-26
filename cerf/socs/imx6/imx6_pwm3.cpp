#include "imx6_pwm.h"
namespace {
class Imx6Pwm3 final : public Imx6Pwm<0x02088000u> {
public:
    using Imx6Pwm::Imx6Pwm;

};
}
REGISTER_SERVICE(Imx6Pwm3);
