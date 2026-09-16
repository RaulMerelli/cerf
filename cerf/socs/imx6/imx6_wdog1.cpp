#include "imx6_wdog_impl.h"

namespace {

class Imx6Wdog1 : public cerf_imx6_wdog_detail::Imx6WdogBase<0x020BC000u> {
public:
    using Imx6WdogBase::Imx6WdogBase;
};

}

REGISTER_SERVICE(Imx6Wdog1);
