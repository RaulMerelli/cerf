#include "../../socs/imx6/imx6_gpio_bus.h"
#include "../../socs/imx6/imx6_gpio_source.h"
#include "../../peripherals/ti_tsc2017/tsc2017_host_state.h"
#include "../board_context.h"
#include "../../core/cerf_emulator.h"

#include <cstdint>

namespace {

class KtpMobileTouchGpio : public Imx6GpioInputSource {
public:
    using Imx6GpioInputSource::Imx6GpioInputSource;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && BoardContext::IsKtpMobile(bd->GetBoard());
    }
    void OnReady() override {
        emu_.Get<Imx6GpioBus>().RegisterSource(this);
        emu_.Get<Tsc2017HostState>().SetIrqChangedCallback(&KtpMobileTouchGpio::OnPenChanged, this);
    }

    uint32_t GpioBase() const override { return 0x020B0000u; }

    uint32_t ApplyPadInputs(uint32_t inputs) override {
        if (emu_.Get<Tsc2017HostState>().PenIrqLineHigh())
            inputs |= kPenIrqMask;
        else
            inputs &= ~kPenIrqMask;
        return inputs;
    }
    uint32_t PendingIsr() override { return emu_.Get<Tsc2017HostState>().PenIrqPending() ? kPenIrqMask : 0u; }
    void OnIsrClear(uint32_t value) override {
        if (value & kPenIrqMask) emu_.Get<Tsc2017HostState>().ClearPenIrqPending();
    }

private:
    static constexpr uint32_t kPenIrqMask = 0x00008000u;

    static void OnPenChanged(void* ctx) { static_cast<KtpMobileTouchGpio*>(ctx)->Reevaluate(); }
};

} // namespace

REGISTER_SERVICE(KtpMobileTouchGpio);
