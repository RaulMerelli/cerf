#include "imx6_usdhc_port.h"

namespace {
class Imx6Usdhc3 final : public Imx6UsdhcPort<0x02198000u, 24> {
public:
    using Imx6UsdhcPort::Imx6UsdhcPort;

};
} // namespace
REGISTER_SERVICE(Imx6Usdhc3);
