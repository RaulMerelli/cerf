#include "../freescale_gpio_impl.h"
#include "imx31_id.h"

namespace {

/* MCIMX31RM Table 5-3: GPIO1 at 0x53FC_C000. */
class Imx31Gpio1
    : public cerf_freescale_gpio_detail::FreescaleGpioBase<0x53FCC000u,
                                                           SocId::Imx31> {
    using FreescaleGpioBase::FreescaleGpioBase;
};

}  /* namespace */

REGISTER_SERVICE(Imx31Gpio1);
