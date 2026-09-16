#include "imx6_usdhc_port.h"

namespace {
class Imx6Usdhc4 final : public Imx6UsdhcPort<0x0219C000u, 25, false> {
public:
    using Imx6UsdhcPort::Imx6UsdhcPort;

};
} // namespace
REGISTER_SERVICE(Imx6Usdhc4);
