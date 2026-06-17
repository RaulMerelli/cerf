#include "../../core/service.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../host/host_widget_registry.h"
#include "../../peripherals/pcmcia/pcmcia_card_catalog.h"
#include "../../peripherals/pcmcia/pcmcia_slot.h"
#include "../../peripherals/pcmcia/pcmcia_space_router.h"
#include "../../socs/sa11xx/sa11xx_gpio.h"

#include <cstdint>

namespace {

/* SA-1110 socket 0 (static window PA 0x20000000). Pins from the FT2_410 PDD
   pcmcia.dll socket.c: CF_CD = GPIO6 active-low (GPLR&0x40 set => "No PC Card");
   READY/nIREQ = GPIO5, idle high, low = card ready / I/O-card IRQ asserted. */
constexpr uint32_t kGpioCd  = 6u;
constexpr uint32_t kGpioIrq = 5u;

class SmartBookG138Pcmcia : public Service, public PcmciaSlotHost {
public:
    explicit SmartBookG138Pcmcia(CerfEmulator& emu)
        : Service(emu), slot_(emu, *this, L"PC Card slot") {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::SmartBookG138;
    }

    void OnReady() override {
        emu_.Get<PcmciaSpaceRouter>().ProvideSockets(&slot_, nullptr);
        emu_.Get<HostWidgetRegistry>().Register(&slot_);

        auto& gpio = emu_.Get<Sa11xxGpio>();
        gpio.DriveInputPin(kGpioCd, true);    /* empty: CF_CD idles high (no card) */
        gpio.DriveInputPin(kGpioIrq, true);   /* READY/nIREQ deasserted (idle high) */

        if (emu_.Get<DeviceConfig>().network_enabled) {
            slot_.InsertCard(emu_.Get<PcmciaCardCatalog>().Create("ne2000"));
        }
    }

    void OnShutdown() override { slot_.OnShutdown(); }

    /* PcmciaSlotHost. */
    void OnCardDetectChanged(PcmciaSlot& slot) override {
        const bool present = slot.HasCard();
        slot.SetPowered(present);
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioCd, !present);   /* present => CD low */
    }
    void OnCardIrqAsserted(PcmciaSlot&) override {
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioIrq, false);     /* nIREQ low = asserted */
    }
    void OnCardIrqDeasserted(PcmciaSlot&) override {
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioIrq, true);
    }

private:
    PcmciaSlot slot_;
};

}  /* namespace */

REGISTER_SERVICE(SmartBookG138Pcmcia);
