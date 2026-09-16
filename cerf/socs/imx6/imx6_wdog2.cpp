#include "imx6_wdog_impl.h"

namespace {

class Imx6Wdog2 : public cerf_imx6_wdog_detail::Imx6WdogBase<0x020C0000u> {
public:
    using Imx6WdogBase::Imx6WdogBase;
};

}

REGISTER_SERVICE(Imx6Wdog2);
