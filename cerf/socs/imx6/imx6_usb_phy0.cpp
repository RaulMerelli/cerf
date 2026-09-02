#include "imx6_usb_phy.h"

namespace {
class Imx6UsbPhy0 final : public Imx6UsbPhy<0x020C9000u, 0> {
public:
    using Imx6UsbPhy::Imx6UsbPhy;

};
} // namespace
REGISTER_SERVICE(Imx6UsbPhy0);
