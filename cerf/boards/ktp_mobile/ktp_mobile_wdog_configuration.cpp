#include "../board_context.h"

#include "../../core/cerf_emulator.h"
#include "../../socs/imx6/imx6_wdog_configuration.h"

namespace {

class KtpMobileWdogConfiguration final : public Imx6WdogConfiguration {
public:
    using Imx6WdogConfiguration::Imx6WdogConfiguration;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && BoardContext::IsKtpMobile(board->GetBoard());
    }

    /* IMX6SDLRM Rev.4 §§70.5.3/70.6 requires PDE cleared within 16 s. hmi_ktp400_mobile_v17,
       nk.exe sub_80314218 @ VA 0x80314218 initializes WDOG1 without touching WMCR. */
    bool PowerDownCounterEnabledAfterBoot() const override { return false; }
};

}

REGISTER_SERVICE_AS(KtpMobileWdogConfiguration, Imx6WdogConfiguration);
