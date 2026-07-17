#include "../../core/service.h"

#include "../../core/cerf_emulator.h"
#include "../../socs/vr41xx/vr41xx_giu.h"
#include "../board_context.h"

namespace {

/* pcmcia.dll CardGetStatus -> sub_135078C reads GIUPIODL D12 (GPIO12); the card is
   present when the pin is LOW (active-low), so an empty socket reads HIGH. */
class CasioToricomailCardSlot : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::CasioToricomail;
    }

    void OnReady() override {
        emu_.Get<Vr41xxGiu>().SetPinLevel(12, true);
    }
};

}  /* namespace */

REGISTER_SERVICE(CasioToricomailCardSlot);
