#include "pxa27x_lcd.h"

#include "../../boards/board_context.h"
#include "pxa270_id.h"
#include "../../core/cerf_emulator.h"
#include "../../host/lcd_scan_tick.h"

namespace {

class Pxa27xLcdScanTick : public LcdScanTick {
public:
    using LcdScanTick::LcdScanTick;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Pxa270;
    }

    void OnHostTick() override {
        emu_.Get<Pxa27xLcd>().AdvanceFrame();
    }
};

}

REGISTER_SERVICE_AS(Pxa27xLcdScanTick, LcdScanTick);
