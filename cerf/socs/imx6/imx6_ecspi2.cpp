#include "imx6_ecspi.h"

namespace {

class Imx6Ecspi2 final : public Imx6Ecspi<0x0200C000u> {
public:
    using Imx6Ecspi::Imx6Ecspi;

};

} // namespace

REGISTER_SERVICE(Imx6Ecspi2);
