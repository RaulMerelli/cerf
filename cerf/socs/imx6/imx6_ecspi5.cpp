#include "imx6_ecspi.h"

namespace {

class Imx6Ecspi5 final : public Imx6Ecspi<0x02018000u> {
public:
    using Imx6Ecspi::Imx6Ecspi;

};

} // namespace

REGISTER_SERVICE(Imx6Ecspi5);
