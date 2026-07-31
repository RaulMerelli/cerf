#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/service.h"
#include "../../peripherals/cirrus_pd6710/pd6710_controller.h"
#include "../../peripherals/pcmcia/pcmcia_card_catalog.h"

namespace {

class DevEmuDefaultPcmciaCard : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::Smdk2410DevEmu;
    }

    void OnReady() override {
        if (!emu_.Get<DeviceConfig>().network_enabled) return;
        emu_.Get<Pd6710Controller>().Slot().InsertCard(
            emu_.Get<PcmciaCardCatalog>().Create("ne2000"));
    }
};

}  /* namespace */

REGISTER_SERVICE(DevEmuDefaultPcmciaCard);
