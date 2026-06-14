#include "../freescale_gpio_impl.h"

namespace {

/* MCIMX51RM Table 2-2: GPIO1 at 0x73F8_4000. */
class Imx51Gpio1
    : public cerf_freescale_gpio_detail::FreescaleGpioBase<0x73F84000u,
                                                           SocFamily::iMX51> {
    using FreescaleGpioBase::FreescaleGpioBase;
};

}  /* namespace */

REGISTER_SERVICE(Imx51Gpio1);
