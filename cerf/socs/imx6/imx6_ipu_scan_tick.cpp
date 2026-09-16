#include "imx6_ipu.h"

using Imx6Ipu = imx6_ipu_detail::Imx6Ipu;

namespace {
class Imx6IpuScanTick final : public LcdScanTick {
public:
    using LcdScanTick::LcdScanTick;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnHostTick() override { emu_.Get<Imx6Ipu>().AdvanceScanTick(); }
};
} // namespace
REGISTER_SERVICE_AS(Imx6IpuScanTick, LcdScanTick);
