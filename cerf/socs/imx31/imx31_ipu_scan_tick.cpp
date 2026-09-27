#include "imx31_ipu.h"

#include "../../boards/board_context.h"
#include "imx31_id.h"
#include "../../core/cerf_emulator.h"
#include "../../host/lcd_scan_tick.h"

namespace {

class Imx31IpuScanTick : public LcdScanTick {
public:
    using LcdScanTick::LcdScanTick;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx31;
    }

    void OnHostTick() override {
        emu_.Get<Imx31Ipu>().OnHostTick();
    }
};

}

REGISTER_SERVICE_AS(Imx31IpuScanTick, LcdScanTick);
