#include "nec_mobilepro_900_pcmcia.h"

#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../host/host_widget_registry.h"
#include "../../peripherals/pcmcia/pcmcia_card_catalog.h"
#include "../../peripherals/pcmcia/pcmcia_space_router.h"
#include "../../socs/pxa255/pxa255_gpio.h"
#include "../board_context.h"

namespace {

/* Per-socket GPIO (Linux jlime_v2.6.34-hpc_mp900.c mp900_pin_config[]). */
constexpr uint32_t kGpioNcd    [2] = { 11u, 13u };  /* nCD,  socket 0 CF / 1 PC */
constexpr uint32_t kGpioPrdy   [2] = {  5u,  7u };  /* PRDY (ready), idles high  */
constexpr uint32_t kGpioCardIrq[2] = { 75u, 76u };  /* card IREQ, active-low      */

}  /* namespace */

NecMobilePro900Pcmcia::NecMobilePro900Pcmcia(CerfEmulator& emu)
    : Service(emu),
      slot0_(emu, *this, L"CompactFlash slot"),
      slot1_(emu, *this, L"PC Card slot") {}

bool NecMobilePro900Pcmcia::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::NecMobilePro900;
}

void NecMobilePro900Pcmcia::OnReady() {
    emu_.Get<PcmciaSpaceRouter>().ProvideSockets(&slot0_, &slot1_);
    auto& widgets = emu_.Get<HostWidgetRegistry>();
    widgets.Register(&slot0_);
    widgets.Register(&slot1_);

    auto& gpio = emu_.Get<Pxa255Gpio>();
    for (int s = 0; s < 2; ++s) {
        gpio.SetInputLevel(kGpioNcd[s],     true);  /* empty: nCD idles high */
        gpio.SetInputLevel(kGpioPrdy[s],    true);  /* ready line idles high */
        gpio.SetInputLevel(kGpioCardIrq[s], true);  /* IREQ idles high (active-low) */
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
    emu_.Get<Pxa255Gpio>().SetInputLevel(kGpioCardIrq[SocketOf(slot)], false);
}

void NecMobilePro900Pcmcia::OnCardIrqDeasserted(PcmciaSlot& slot) {
    emu_.Get<Pxa255Gpio>().SetInputLevel(kGpioCardIrq[SocketOf(slot)], true);
}

REGISTER_SERVICE(NecMobilePro900Pcmcia);
