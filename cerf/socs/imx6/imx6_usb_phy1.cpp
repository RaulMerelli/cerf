#include "imx6_usb_phy.h"

namespace {
class Imx6UsbPhy1 final : public Imx6UsbPhy<0x020CA000u, 1> {
public:
    using Imx6UsbPhy::Imx6UsbPhy;

};
} // namespace
REGISTER_SERVICE(Imx6UsbPhy1);
