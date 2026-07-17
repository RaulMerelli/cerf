#include "../vr41xx/vr41xx_rtc.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"

#include <cstdint>

namespace {

/* TClock = (18.432 MHz / CLKSP) * 16 (UM p245 CLKSPEEDREG); MobilePro 700
   straps CLKSP=9 (66-MHz VR4102 -> PClock 65.536 MHz), so TClock = 32.768 MHz
   (25-bit TCLK period 1.024 s, matching UM p335 "1 to 2 seconds"). */
class Vr4102Rtc : public Vr41xxRtc {
public:
    using Vr41xxRtc::Vr41xxRtc;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4102;
    }
    uint32_t TClockHz() const override { return 32768000u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(Vr4102Rtc, Vr41xxRtc);
