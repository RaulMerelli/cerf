#include "ktp_mobile_f_module_device.h"

#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../socs/imx6/imx6_gpio_bus.h"
#include "../../socs/imx6/imx6_gpio_source.h"

#include <cstdint>

namespace {

/* hmi_ktp400_mobile_v13 HandshakeDriver.dll HSK_IOControl @ 0xEF481DD8 and
   FModuleService.dll sub_EF1F95D8: GPIO4_IO27 is driven high, low, high; the
   low-to-high release completes a warm reset of the companion STM32. */
class KtpMobileFModuleResetGpio final : public Imx6GpioInputSource {
public:
    using Imx6GpioInputSource::Imx6GpioInputSource;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        if (!board) return false;
        switch (board->GetBoard()) {
        case Board::HmiKtp400FMobile:
        case Board::HmiKtp700FMobile:
        case Board::HmiKtp900FMobile:
        case Board::HmiKtp700FHwMobile:
        case Board::HmiKtp700FArcticMobile:
        case Board::HmiTp1000fMobile: return true;
        default: return false;
        }
    }

    void OnReady() override { emu_.Get<Imx6GpioBus>().RegisterSource(this); }
    uint32_t GpioBase() const override { return 0x020A8000u; } /* GPIO4 */

    void OnEffectiveOutputs(uint32_t dr, uint32_t gdir) override {
        /* Before the guest makes the pin an output the external reset line is
           released.  Once configured, the output latch is the effective
           electrical level observed by the daughterboard. */
        const bool high = (gdir & kResetMask) == 0u || (dr & kResetMask) != 0u;
        if (!level_known_ || high != line_high_) {
            level_known_ = true;
            line_high_ = high;
            emu_.Get<KtpMobileFModuleDevice>().ObserveModuleReset(high);
        }
    }

    void SaveState(StateWriter& w) override {
        w.Write(static_cast<uint8_t>(level_known_));
        w.Write(static_cast<uint8_t>(line_high_));
    }
    void RestoreState(StateReader& r) override {
        uint8_t known = 0;
        uint8_t high = 0;
        r.Read(known);
        r.Read(high);
        level_known_ = known != 0u;
        line_high_ = high != 0u;
    }
    void OnControllerReset(ResetLineKind) override {
        level_known_ = false;
        line_high_ = true;
    }

private:
    static constexpr uint32_t kResetMask = 1u << 27u;
    bool level_known_ = false;
    bool line_high_ = true;
};

} // namespace

REGISTER_SERVICE(KtpMobileFModuleResetGpio);
