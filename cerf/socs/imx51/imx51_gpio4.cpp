#include "../freescale_gpio_impl.h"

namespace {

/* MCIMX51RM Table 2-2: GPIO4 at 0x73F9_0000. */
class Imx51Gpio4
    : public cerf_freescale_gpio_detail::FreescaleGpioBase<0x73F90000u,
                                                           SocFamily::iMX51> {
    using FreescaleGpioBase::FreescaleGpioBase;
};

}  /* namespace */

REGISTER_SERVICE(Imx51Gpio4);
