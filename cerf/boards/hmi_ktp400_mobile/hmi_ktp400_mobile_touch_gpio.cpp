#include "../../socs/imx6/imx6_gpio_bus.h"
#include "../../socs/imx6/imx6_gpio_source.h"
#include "../../peripherals/ti_tsc2017/ti_tsc2017_host_state.h"
#include "../board_context.h"
#include "../../core/cerf_emulator.h"

#include <cstdint>

namespace {

/* KTP400 TSC2017 /PENIRQ is wired to GPIO6_IO15. This drives the pen input
   level + pending ISR from Tsc2017HostState and re-drives the GPIO6 interrupt
   when the host asserts /PENIRQ off the JIT thread. */
class HmiKtp400MobileTouchGpio : public Imx6GpioInputSource {
public:
    using Imx6GpioInputSource::Imx6GpioInputSource;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::HmiKtp400Mobile;
    }
    void OnReady() override {
        emu_.Get<Imx6GpioBus>().RegisterSource(this);
        emu_.Get<Tsc2017HostState>().SetIrqChangedCallback(
            &HmiKtp400MobileTouchGpio::OnPenChanged, this);
    }

    uint32_t GpioBase() const override { return 0x020B0000u; }  /* GPIO6 */

    uint32_t ApplyPadInputs(uint32_t inputs) override {
        if (emu_.Get<Tsc2017HostState>().PenIrqLineHigh())
            inputs |= kPenIrqMask;
        else
            inputs &= ~kPenIrqMask;
        return inputs;
    }
    uint32_t PendingIsr() override {
        return emu_.Get<Tsc2017HostState>().PenIrqPending() ? kPenIrqMask : 0u;
    }
    void OnIsrClear(uint32_t value) override {
        if (value & kPenIrqMask)
            emu_.Get<Tsc2017HostState>().ClearPenIrqPending();
    }

private:
    static constexpr uint32_t kPenIrqMask = 0x00008000u;  /* GPIO6_IO15 */

    static void OnPenChanged(void* ctx) {
        static_cast<HmiKtp400MobileTouchGpio*>(ctx)->Reevaluate();
    }
};

}  // namespace

REGISTER_SERVICE(HmiKtp400MobileTouchGpio);

