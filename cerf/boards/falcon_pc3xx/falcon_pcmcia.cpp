#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/service.h"
#include "../../host/host_widget_registry.h"
#include "../../peripherals/pcmcia/pcmcia_card_catalog.h"
#include "../../peripherals/pcmcia/pcmcia_slot.h"
#include "../../peripherals/pcmcia/pcmcia_space_router.h"
#include "../../socs/pxa255/pxa255_gpio.h"

namespace {

/* Pin map from the ROM PCMCIA PDD (pcmcia.dll status read sub_18C21A4):
   CD = GPIO10 (GPLR0 bit10, low = present), ready/nIREQ = GPIO33 (GPLR1
   bit1, low = asserted). Vcc modeled present-while-occupied: the PDD's
   MQ1188 MQSS0C writes are bus-buffer/voltage config, not a Vcc gate. */
constexpr uint32_t kGpioCd    = 10u;
constexpr uint32_t kGpioReady = 33u;

class FalconPcmcia : public Service, public PcmciaSlotHost {
public:
    explicit FalconPcmcia(CerfEmulator& emu)
        : Service(emu), slot_(emu, *this, L"CF / PC Card slot") {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::FalconPC3xx;
    }

    void OnReady() override {
        emu_.Get<PcmciaSpaceRouter>().ProvideSockets(&slot_, nullptr);
        emu_.Get<HostWidgetRegistry>().Register(&slot_);

        auto& gpio = emu_.Get<Pxa255Gpio>();
        gpio.SetInputLevel(kGpioCd, true);     /* empty slot: CD idles high */
        gpio.SetInputLevel(kGpioReady, true);

        if (emu_.Get<DeviceConfig>().network_enabled) {
            slot_.InsertCard(emu_.Get<PcmciaCardCatalog>().Create("ne2000"));
        }
    }

    void OnShutdown() override { slot_.OnShutdown(); }

    /* PcmciaSlotHost. */
    void OnCardDetectChanged(PcmciaSlot& slot) override {
        const bool present = slot.HasCard();
        slot.SetPowered(present);
        emu_.Get<Pxa255Gpio>().SetInputLevel(kGpioCd, !present);
    }
    void OnCardIrqAsserted(PcmciaSlot&) override {
        emu_.Get<Pxa255Gpio>().SetInputLevel(kGpioReady, false);
    }
    void OnCardIrqDeasserted(PcmciaSlot&) override {
        emu_.Get<Pxa255Gpio>().SetInputLevel(kGpioReady, true);
    }

private:
    PcmciaSlot slot_;
};

}  /* namespace */

REGISTER_SERVICE(FalconPcmcia);
