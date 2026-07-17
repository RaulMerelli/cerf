#include "../../socs/vr41xx/vr41xx_piu_panel.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../board_context.h"

#include <cstdint>
#include <optional>

namespace {

/* PIUCMDREG ADCMD(3:0): 0100 ADIN0, 0101 ADIN1, 0110 ADIN2 (VR4102 UM 19.3.5 (2/2)). */
enum : uint16_t { kAdcmdAdin0 = 4, kAdcmdAdin1 = 5, kAdcmdAdin2 = 6 };

/* touch.dll sub_15A0E24 computes the raw coordinate as 379*ADINx/ADIN2, so ADIN2 = 379
   yields the same [0,1023] extents the coordinate page buffers produce. */
constexpr uint16_t kAdin2Reference = 379u;

/* touch.dll sub_15A0BB0 treats a contact as valid at Z >= 0x340. */
constexpr uint16_t kPressureContact = 0x03FFu;

class NecMobilePro700TouchPanel : public Vr41xxPiuPanel {
public:
    using Vr41xxPiuPanel::Vr41xxPiuPanel;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::NecMobilePro700;
    }

    std::optional<uint16_t> ConvertCommandPort(uint16_t adcmd,
                                               uint16_t pos_x,
                                               uint16_t pos_y) override {
        switch (adcmd) {
            case kAdcmdAdin0: return pos_x;
            case kAdcmdAdin1: return pos_y;
            case kAdcmdAdin2: return kAdin2Reference;
            default:
                LOG(Caution, "NecMobilePro700TouchPanel: PIUCMDREG ADCMD=0x%X selects an "
                        "A/D port this board's panel does not drive\n", adcmd);
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
    }

    std::optional<uint16_t> PressureSample() override { return kPressureContact; }

    std::optional<uint16_t> AdPortScanSample(uint16_t port) override {
        LOG(Caution, "NecMobilePro700TouchPanel: ADPortScan A/D port 0x%X is not modeled on "
                "this board\n", port);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecMobilePro700TouchPanel, Vr41xxPiuPanel);
