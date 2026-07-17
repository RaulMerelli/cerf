#include "../vr41xx/vr41xx_rtc.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"

#include <cstdint>

namespace {

/* VR4121 UM 11.2.10 p291: TClock = PClock / DIVT[3:0], PClock = (18.432 MHz /
   CLKSP[4:0]) * 64; CERF models no CLKSPEEDREG, so the strap is ungrounded. */
class Vr4121Rtc : public Vr41xxRtc {
public:
    using Vr41xxRtc::Vr41xxRtc;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4121;
    }
    uint32_t TClockHz() const override { return 0u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(Vr4121Rtc, Vr41xxRtc);
