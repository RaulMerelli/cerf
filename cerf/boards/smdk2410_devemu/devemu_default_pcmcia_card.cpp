#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/service.h"
#include "../../peripherals/cirrus_pd6710/pd6710_controller.h"
#include "../../peripherals/pcmcia/pcmcia_auto_insert.h"

namespace {

class DevEmuDefaultPcmciaCard : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::Smdk2410DevEmu;
    }

    void OnReady() override {
        emu_.Get<PcmciaAutoInsert>().InsertDefaultNetworkCard(
            emu_.Get<Pd6710Controller>().Slot());
    }
};

}  /* namespace */

REGISTER_SERVICE(DevEmuDefaultPcmciaCard);
