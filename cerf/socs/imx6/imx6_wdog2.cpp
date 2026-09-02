#include "imx6_wdog_impl.h"

namespace {

/* IMX6SDLRM Rev.4 section 70.7 memory map: WDOG2 at 20C_0000. */
class Imx6Wdog2 : public cerf_imx6_wdog_detail::Imx6WdogBase<0x020C0000u> {
public:
    using Imx6WdogBase::Imx6WdogBase;
};

} /* namespace */

REGISTER_SERVICE(Imx6Wdog2);
