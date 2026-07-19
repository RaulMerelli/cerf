#include "../../core/service.h"

#include "../../core/cerf_emulator.h"
#include "../../socs/vr41xx/vr41xx_giu.h"
#include "../board_context.h"

namespace {

/* GIUPIODL D9/D10/D11 = GPIO9/10/11 (VR4131 UM U15350EJ2V0UM 14.2.3). nk_main_kernel.exe
   cold boot requires GPIO11=H (sub_9F032E0C @0x9F032E0C), GPIO9=H + GPIO10=L (sub_9F032ED4
   @0x9F032ED4), and all three (sub_9F032F20 @0x9F032F20). */
class CasioCassiopeiaEm500PowerGpio : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::CasioCassiopeiaEm500;
    }

    void OnReady() override {
        auto& giu = emu_.Get<Vr41xxGiu>();
        giu.SetPinLevel(11, true);
        giu.SetPinLevel(9,  true);
        giu.SetPinLevel(10, false);
    }
};

}  /* namespace */

REGISTER_SERVICE(CasioCassiopeiaEm500PowerGpio);
