#include "../../core/service.h"

#include "../../core/cerf_emulator.h"
#include "../board_detector.h"
#include "../../socs/pxa255/pxa255_gpio.h"

namespace {

/* PCMCIA card-detect: GPIO10 idles high = empty slot. Without it the bus enum
   identifies a phantom card and deadlocks device.exe on the unknown-card
   GweApiSetReady wait, behind which GWES launches. */
class FalconPcmciaCardDetect : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::FalconPC3xx;
    }

    void OnReady() override {
        emu_.Get<Pxa255Gpio>().SetInputLevel(10u, true);
    }
};

}  /* namespace */

REGISTER_SERVICE(FalconPcmciaCardDetect);
