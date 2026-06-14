#include "../freescale_gpio_impl.h"

namespace {

/* MCIMX51RM Table 2-2: GPIO3 at 0x73F8_C000. SBOOT configures its output pins on
   the IOMUXC/GPIO bring-up path (Bootloader.bin 0x8FF058DC; DR read @0x8FF05944). */
class Imx51Gpio3
    : public cerf_freescale_gpio_detail::FreescaleGpioBase<0x73F8C000u,
                                                           SocFamily::iMX51> {
    using FreescaleGpioBase::FreescaleGpioBase;
};

}  /* namespace */

REGISTER_SERVICE(Imx51Gpio3);
