#include "imx6_usdhc_port.h"

namespace {
class Imx6Usdhc2 final : public Imx6UsdhcPort<0x02194000u, 23, false> {
public:
    using Imx6UsdhcPort::Imx6UsdhcPort;

};
} // namespace
REGISTER_SERVICE(Imx6Usdhc2);
