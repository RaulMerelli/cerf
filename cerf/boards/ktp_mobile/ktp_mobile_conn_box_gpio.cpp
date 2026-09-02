#include "../../socs/imx6/imx6_gpio_bus.h"
#include "../../socs/imx6/imx6_gpio_source.h"
#include "../board_context.h"
#include "../../core/cerf_emulator.h"

#include <cstdint>

namespace {

/* KTP400 ConnBox/readBoxID compat mode bit-bangs the MicroOMS link on GPIO1:
   IO07 is the host strobe, IO08 the target ACK. The first read after a rising
   strobe reports ACK low (peer has seen the edge); the next reports ACK high
   (peer response complete). */
class KtpMobileConnBoxGpio : public Imx6GpioInputSource {
public:
    using Imx6GpioInputSource::Imx6GpioInputSource;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && BoardContext::IsKtpMobile(bd->GetBoard());
    }
    void OnReady() override { emu_.Get<Imx6GpioBus>().RegisterSource(this); }

    uint32_t GpioBase() const override { return 0x0209C000u; } /* GPIO1 */

    uint32_t ApplyDataRead(uint32_t value) override {
        if ((value & 0x00000080u) == 0u) {
            armed_ = false;
            value &= ~0x00000100u; /* GPIO1_IO08 low */
        } else if (!armed_) {
            armed_ = true;
            value &= ~0x00000100u; /* first high-strobe sample */
        } else {
            value |= 0x00000100u; /* peer ACK */
        }
        return value;
    }

    void SaveState(StateWriter& w) override { w.Write(static_cast<uint8_t>(armed_ ? 1u : 0u)); }
    void RestoreState(StateReader& r) override {
        uint8_t a = 0;
        r.Read(a);
        armed_ = a != 0;
    }
    void OnControllerReset(ResetLineKind) override { armed_ = false; }

private:
    bool armed_ = false;
};

} // namespace

REGISTER_SERVICE(KtpMobileConnBoxGpio);
