#include "imx6_ipu.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../host/lcd_scan_tick.h"
#include "imx6_id.h"

namespace {
class Imx6IpuScanTick final : public LcdScanTick {
public:
    using LcdScanTick::LcdScanTick;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx6;
    }
    void OnHostTick() override { emu_.Get<Imx6Ipu>().AdvanceScanTick(); }
};
}
REGISTER_SERVICE_AS(Imx6IpuScanTick, LcdScanTick);
