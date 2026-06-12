#include "simpad_sl4_cs3_sink.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../host/host_widget_registry.h"
#include "../../peripherals/pcmcia/pcmcia_card_catalog.h"
#include "../../peripherals/pcmcia/pcmcia_slot.h"
#include "../../peripherals/pcmcia/pcmcia_space_router.h"
#include "../../socs/sa11xx/sa11xx_gpio.h"

namespace {

/* SIMpad SL4 single PC Card socket (SA-1110 socket 0, window PA 0x20000000).
   simpad_v2.6.39.h + ROM pcmcia.dll: CF_CD = GPIO24 active-low (0 = present;
   GetStatus sub_12F2590 tests GPLR bit24), CF_IRQ/READY = GPIO1. Vcc + RESET
   are driven by the PDD through the CS3 latch (observed via SimpadSl4Cs3Sink). */
constexpr uint32_t kGpioCd  = 24u;   /* GPIO_CF_CD  */
constexpr uint32_t kGpioIrq = 1u;    /* GPIO_CF_IRQ */

/* CS3 latch PCMCIA bits (simpad_v2.6.39.h). */
constexpr uint16_t kVcc5vEn     = 0x0001;
constexpr uint16_t kVcc3vEn     = 0x0002;
constexpr uint16_t kPcmciaReset = 0x0080;

class SimpadSl4Pcmcia : public SimpadSl4Cs3Sink, public PcmciaSlotHost {
public:
    explicit SimpadSl4Pcmcia(CerfEmulator& emu)
        : SimpadSl4Cs3Sink(emu), slot_(emu, *this, L"PC Card slot") {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::SimpadSl4;
    }

    void OnReady() override {
        emu_.Get<PcmciaSpaceRouter>().ProvideSockets(&slot_, nullptr);
        emu_.Get<HostWidgetRegistry>().Register(&slot_);

        auto& gpio = emu_.Get<Sa11xxGpio>();
        gpio.DriveInputPin(kGpioCd, true);    /* empty: CF_CD idles high     */
        gpio.DriveInputPin(kGpioIrq, true);   /* READY/nIREQ deasserted high */

        if (emu_.Get<DeviceConfig>().network_enabled) {
            slot_.InsertCard(emu_.Get<PcmciaCardCatalog>().Create("ne2000"));
        }
    }

    void OnShutdown() override { slot_.OnShutdown(); }

    /* CS3 latch: the PDD drives Vcc (PDCardSetAdapter sub_12F2D90) and pulses
       the RESET pin (PDCardResetSocket sub_12F28F0) here; the card resets on
       the RESET-line release edge. */
    void OnCs3LatchChanged(uint16_t latch) override {
        slot_.SetPowered((latch & (kVcc5vEn | kVcc3vEn)) != 0);
        const bool reset = (latch & kPcmciaReset) != 0;
        if (reset_prev_ && !reset) slot_.ResetCard();
        reset_prev_ = reset;
    }

    /* PcmciaSlotHost. */
    void OnCardDetectChanged(PcmciaSlot& slot) override {
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioCd, !slot.HasCard());
    }
    void OnCardIrqAsserted(PcmciaSlot&) override {
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioIrq, false);
    }
    void OnCardIrqDeasserted(PcmciaSlot&) override {
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioIrq, true);
    }

private:
    PcmciaSlot slot_;
    bool       reset_prev_ = false;
};

}  /* namespace */

REGISTER_SERVICE_AS(SimpadSl4Pcmcia, SimpadSl4Cs3Sink);
