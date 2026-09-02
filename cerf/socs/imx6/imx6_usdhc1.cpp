#include "imx6_usdhc_port.h"

namespace {
class Imx6Usdhc1 final : public Imx6UsdhcPort<0x02190000u, 22, false> {
public:
    using Imx6UsdhcPort::Imx6UsdhcPort;

};
} // namespace
REGISTER_SERVICE(Imx6Usdhc1);
