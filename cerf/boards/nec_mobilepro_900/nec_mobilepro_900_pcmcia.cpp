#include "nec_mobilepro_900_pcmcia.h"

#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../host/host_widget_registry.h"
#include "../../peripherals/pcmcia/pcmcia_card_catalog.h"
#include "../../peripherals/pcmcia/pcmcia_space_router.h"
#include "../../socs/pxa255/pxa255_gpio.h"
#include "../board_detector.h"

namespace {

/* PXA255 GPIO sense lines per socket, read by the pcmcia.dll PDD via GPLR
   (sub_1B02244 sets GPDR0 bits 5/7/11/13 as inputs; ready threads sub_1B021B8/
   sub_1B02170 read GPLR bits 5/7). nCD active-low (low = present); PRDY high =
   ready. Drive nCD high for an empty slot or the PDD detects a phantom card. */
constexpr uint32_t kGpioNcd [2] = { 11u, 13u };   /* socket 0 CF, 1 PC Card */
constexpr uint32_t kGpioPrdy[2] = {  5u,  7u };

}  /* namespace */

NecMobilePro900Pcmcia::NecMobilePro900Pcmcia(CerfEmulator& emu)
    : Service(emu),
      slot0_(emu, *this, L"CompactFlash slot"),
      slot1_(emu, *this, L"PC Card slot") {}

bool NecMobilePro900Pcmcia::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetBoard() == Board::NecMobilePro900;
}

void NecMobilePro900Pcmcia::OnReady() {
    emu_.Get<PcmciaSpaceRouter>().ProvideSockets(&slot0_, &slot1_);
    auto& widgets = emu_.Get<HostWidgetRegistry>();
    widgets.Register(&slot0_);
    widgets.Register(&slot1_);

    auto& gpio = emu_.Get<Pxa255Gpio>();
    for (int s = 0; s < 2; ++s) {
        gpio.SetInputLevel(kGpioNcd[s],  true);   /* empty: nCD idles high */
        gpio.SetInputLevel(kGpioPrdy[s], true);   /* ready line idles high */
    }

    if (emu_.Get<DeviceConfig>().network_enabled) {
        slot1_.InsertCard(emu_.Get<PcmciaCardCatalog>().Create("ne2000"));
    }
}

void NecMobilePro900Pcmcia::OnShutdown() {
    slot0_.OnShutdown();
    slot1_.OnShutdown();
}

int NecMobilePro900Pcmcia::SocketOf(const PcmciaSlot& slot) const {
    return (&slot == &slot1_) ? 1 : 0;
}

void NecMobilePro900Pcmcia::ResetSocket(int socket) {
    (socket == 1 ? slot1_ : slot0_).ResetCard();
}

bool NecMobilePro900Pcmcia::SocketHasCard(int socket) const {
    return (socket == 1 ? slot1_ : slot0_).HasCard();
}

void NecMobilePro900Pcmcia::OnCardDetectChanged(PcmciaSlot& slot) {
    const int  socket  = SocketOf(slot);
    const bool present = slot.HasCard();
    slot.SetPowered(present);
    emu_.Get<Pxa255Gpio>().SetInputLevel(kGpioNcd[socket], !present);
}

void NecMobilePro900Pcmcia::OnCardIrqAsserted(PcmciaSlot& slot) {
    emu_.Get<Pxa255Gpio>().SetInputLevel(kGpioPrdy[SocketOf(slot)], false);
}

void NecMobilePro900Pcmcia::OnCardIrqDeasserted(PcmciaSlot& slot) {
    emu_.Get<Pxa255Gpio>().SetInputLevel(kGpioPrdy[SocketOf(slot)], true);
}

REGISTER_SERVICE(NecMobilePro900Pcmcia);
