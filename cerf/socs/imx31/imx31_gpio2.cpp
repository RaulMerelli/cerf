#include "../freescale_gpio_impl.h"

namespace {

/* MCIMX31RM Table 5-3: GPIO2 at 0x53FD_0000. */
class Imx31Gpio2
    : public cerf_freescale_gpio_detail::FreescaleGpioBase<0x53FD0000u,
                                                           SocFamily::iMX31> {
    using FreescaleGpioBase::FreescaleGpioBase;
};

}  /* namespace */

REGISTER_SERVICE(Imx31Gpio2);
