#include "../../socs/imx6/imx6_gpio_bus.h"
#include "../../socs/imx6/imx6_gpio_source.h"
#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../socs/imx6/imx6_uart2.h"

#include <cstdint>

namespace {

class KtpMobileConnBoxGpio : public Imx6GpioInputSource {
public:
    using Imx6GpioInputSource::Imx6GpioInputSource;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && BoardContext::IsKtpMobile(bd->GetBoard());
    }
    void OnReady() override { emu_.Get<Imx6GpioBus>().RegisterSource(this); }

    // i.MX 6Solo/6DualLite Reference Manual Rev.4 Table 2-2 maps GPIO1 at 0x0209C000.
    uint32_t GpioBase() const override { return 0x0209C000u; }

    // hmi_ktp400_mobile_v13, ConnBox.dll: sub_EF492B54 @ VA 0xEF492B54 toggles GPIO bit 0x80,
    // waits for bit 0x100, then reads two UART bytes; a second byte matching
    // (value & 0xE0) == 0xA0 is BoxType 3.
    uint32_t ApplyDataRead(uint32_t value) override {
        if ((value & 0x00000080u) == 0u) {
            armed_ = false;
            value &= ~0x00000100u;
        } else if (!armed_) {
            armed_ = true;
            value &= ~0x00000100u;
        } else {
            if (!box_id_sent_) {
                const uint8_t id[] = {kBoxIdStub, kBoxType3Response};
                emu_.Get<Imx6Uart2>().InjectRx(id, sizeof(id));
                box_id_sent_ = true;
            }
            value |= 0x00000100u;
        }
        return value;
    }

    void SaveState(StateWriter& w) override {
        w.Write(static_cast<uint8_t>(armed_ ? 1u : 0u));
        w.Write(static_cast<uint8_t>(box_id_sent_ ? 1u : 0u));
    }
    void RestoreState(StateReader& r) override {
        uint8_t a = 0;
        uint8_t b = 0;
        r.Read(a);
        r.Read(b);
        armed_ = a != 0;
        box_id_sent_ = b != 0;
    }
    void OnControllerReset(ResetLineKind) override {
        armed_ = false;
        box_id_sent_ = false;
    }

private:
    static constexpr uint8_t kBoxIdStub = 0u;
    static constexpr uint8_t kBoxType3Response = 0xA0u;

    bool armed_ = false;
    bool box_id_sent_ = false;
};

} // namespace

REGISTER_SERVICE(KtpMobileConnBoxGpio);
