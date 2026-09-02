#include "../../core/cerf_emulator.h"
#include "../../socs/imx6/imx6_gpio_bus.h"
#include "../../socs/imx6/imx6_gpio_source.h"
#include "../board_context.h"
#include "ktp_mobile_f_module_device.h"

#include <cstdint>

namespace {

/* hmi_ktp400_mobile_v13 HandshakeDriver.dll HSK_IOControl @ 0xEF481DD8:
   selector 1 reads GPIO5_IO05 (module READY), selector 2 reads/writes
   GPIO5_IO06 (panel ACK/control). */
class KtpMobileFModuleGpio final : public Imx6GpioInputSource {
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

    void OnReady() override {
        emu_.Get<Imx6GpioBus>().RegisterSource(this);
        emu_.Get<KtpMobileFModuleDevice>().SetReadyChangedCallback(
            &KtpMobileFModuleGpio::OnReadyChanged, this);
        last_ready_ = emu_.Get<KtpMobileFModuleDevice>().ReadyLevel();
        Reevaluate();
    }

    uint32_t GpioBase() const override { return 0x020AC000u; } // GPIO5

    uint32_t ApplyPadInputs(uint32_t inputs) override {
        return ApplyReady(inputs);
    }

    uint32_t ApplyDataRead(uint32_t data) override { return ApplyReady(data); }

    uint32_t PendingIsr() override {
        return ready_rise_pending_ ? kReadyMask : 0u;
    }

    void OnIsrClear(uint32_t value) override {
        if (value & kReadyMask) {
            ready_rise_pending_ = false;
        }
    }

    void OnEffectiveOutputs(uint32_t dr, uint32_t gdir) override {
        if (gdir & kAckMask)
            emu_.Get<KtpMobileFModuleDevice>().ObservePanelAcknowledge(
                (dr & kAckMask) != 0u);
    }

    void SaveState(StateWriter& w) override {
        emu_.Get<KtpMobileFModuleDevice>().SaveState(w);
        w.Write(static_cast<uint8_t>(last_ready_));
        w.Write(static_cast<uint8_t>(ready_rise_pending_));
    }
    void RestoreState(StateReader& r) override {
        emu_.Get<KtpMobileFModuleDevice>().RestoreState(r);
        uint8_t last_ready = 0;
        uint8_t pending = 0;
        r.Read(last_ready);
        r.Read(pending);
        last_ready_ = last_ready != 0;
        ready_rise_pending_ = pending != 0;
    }
    void PostRestore() override {
        emu_.Get<KtpMobileFModuleDevice>().PostRestore();
        Reevaluate();
    }
    void OnControllerReset(ResetLineKind) override {
        ready_rise_pending_ = false;
        last_ready_ = emu_.Get<KtpMobileFModuleDevice>().ReadyLevel();
    }

private:
    static constexpr uint32_t kReadyMask = 1u << 5u;
    static constexpr uint32_t kAckMask = 1u << 6u;

    uint32_t ApplyReady(uint32_t value) const {
        if (emu_.Get<KtpMobileFModuleDevice>().ReadyLevel())
            return value | kReadyMask;
        return value & ~kReadyMask;
    }

    static void OnReadyChanged(void* context) {
        auto* self = static_cast<KtpMobileFModuleGpio*>(context);
        const bool ready = self->emu_.Get<KtpMobileFModuleDevice>().ReadyLevel();
        if (ready && !self->last_ready_) self->ready_rise_pending_ = true;
        self->last_ready_ = ready;
        self->Reevaluate();
    }

    bool last_ready_ = false;
    bool ready_rise_pending_ = false;
};

} // namespace

REGISTER_SERVICE(KtpMobileFModuleGpio);
