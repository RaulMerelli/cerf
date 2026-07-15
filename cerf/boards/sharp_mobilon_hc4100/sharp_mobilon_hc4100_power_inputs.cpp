#include "../../socs/pr31x00/pr31x00_power_inputs.h"

#include "../../core/cerf_emulator.h"
#include "../board_context.h"
#include "sharp_mobilon_hc4100_battery.h"

namespace {

class SharpMobilonHc4100PowerInputs : public Pr31x00PowerInputs {
public:
    using Pr31x00PowerInputs::Pr31x00PowerInputs;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SharpMobilonHc4100;
    }

    bool PwrIntAsserted() const override {
        return !emu_.Get<SharpMobilonHc4100Battery>().IsOnBattery();
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(SharpMobilonHc4100PowerInputs, Pr31x00PowerInputs);
