#include "../../host/host_window.h"
#include "../../host/touch_input.h"
#include "../../peripherals/ti_tsc2046/ti_tsc2046_touch.h"
#include "../../socs/omap3530/omap3530_gpio_bus.h"
#include "../board_detector.h"

#include "../../core/cerf_emulator.h"

#include <algorithm>
#include <cstdint>

namespace {

/* PenGPIO=0xAF=175 from BSP platform.reg:471. Pin 175 = GPIO6 bit 15. */
constexpr uint32_t kPenIrqGpio  = 175;
constexpr uint32_t kAdcRangeMax = 4095;

class OmapEvm3530TouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        return emu_.Get<BoardDetector>().GetBoard() == Board::OmapEvm3530;
    }

    void OnPenDown    (int x, int y) override { Apply(x, y, true);  }
    void OnPenMove    (int x, int y) override { Apply(x, y, true);  }
    void OnPenUp      (int x, int y) override { Apply(x, y, false); }
    void OnCaptureLost()             override { ApplyUp(); }

private:
    static uint16_t ToAdc(int pixel, int screen_extent) {
        if (screen_extent <= 0) return 0;
        const long scaled = static_cast<long>(pixel) *
                            static_cast<long>(kAdcRangeMax) /
                            static_cast<long>(screen_extent);
        return static_cast<uint16_t>(
            std::clamp<long>(scaled, 0, static_cast<long>(kAdcRangeMax)));
    }

    void Apply(int host_x, int host_y, bool pen_down) {
        auto& hw  = emu_.Get<HostWindow>();
        const uint16_t adc_x = ToAdc(host_x, hw.ClientWidth());
        const uint16_t adc_y = ToAdc(host_y, hw.ClientHeight());
        emu_.Get<Tsc2046Touch>().SetState(adc_x, adc_y, pen_down);
        /* PENIRQ is active-low: drive pin low while pen is down. */
        emu_.Get<Omap3530GpioBus>().SetInputPin(kPenIrqGpio, !pen_down);
    }

    void ApplyUp() {
        emu_.Get<Tsc2046Touch>().SetState(0, 0, false);
        emu_.Get<Omap3530GpioBus>().SetInputPin(kPenIrqGpio, true);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(OmapEvm3530TouchInput, TouchInput);
