#include "../freescale_gpio_impl.h"

namespace {

/* MCIMX31RM Table 5-3: GPIO1 at 0x53FC_C000. */
class Imx31Gpio1
    : public cerf_freescale_gpio_detail::FreescaleGpioBase<0x53FCC000u,
                                                           SocFamily::iMX31> {
    using FreescaleGpioBase::FreescaleGpioBase;
};

}  /* namespace */

REGISTER_SERVICE(Imx31Gpio1);
