#include "imx6_ecspi.h"

namespace {

class Imx6Ecspi1 final : public Imx6Ecspi<0x02008000u> {
public:
    using Imx6Ecspi::Imx6Ecspi;

};

} // namespace

REGISTER_SERVICE(Imx6Ecspi1);
