#include "imx6_wdog_configuration.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"

namespace {

class Imx6WdogConfigurationDefault final : public Imx6WdogConfiguration {
public:
    using Imx6WdogConfiguration::Imx6WdogConfiguration;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSoc() == SocFamily::iMX6;
    }

    /* IMX6SDLRM Rev.4 §70.7.5: WMCR.PDE resets to 1. */
    bool PowerDownCounterEnabledAfterBoot() const override { return true; }
};

} // namespace

REGISTER_SERVICE_AS_FALLBACK(Imx6WdogConfigurationDefault, Imx6WdogConfiguration);
