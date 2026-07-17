#include "../../socs/vr41xx/vr41xx_piu_panel.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../board_context.h"
#include "casio_toricomail_battery.h"

#include <cstdint>
#include <optional>

namespace {

/* PIUCMDREG ADCMD(3:0) = 0000 selects the TPX0 port (VR4121 UM 20.3.5 (2/2)). touch.dll
   sub_13715A0 issues exactly one command scan, with PIUCMDREG = 0xCC0 (TPYEN = 11,
   TPYD = 11, ADCMD = 0000), and no module in the ROM reads PIUAB0REG. */
constexpr uint16_t kAdcmdTpx0 = 0;

/* PIUCMDREG/PIUAMSKREG ADCMD index 4 = ADIN0, 5 = ADIN1 (VR4121 UM 20.3.5 (2/2), 20.3.7). */
constexpr uint16_t kAdcmdAdin0 = 4;
constexpr uint16_t kAdcmdAdin1 = 5;

/* gwes.exe sub_ABEEC (0xABEEC): the ADIN0 average selects the main-battery level - >= 0x3D5
   full, >= 0x3B9 mid, below low. */
constexpr uint16_t kMainFull = 0x3D5u;
constexpr uint16_t kMainMid  = 0x3B9u;
constexpr uint16_t kMainLow  = 0x3B8u;

/* gwes.exe sub_AC38C (0xAC38C): ADIN1 >= 0x39D reads as the healthy second cell. */
constexpr uint16_t kSecondCellHealthy = 0x39Du;

class CasioToricomailTouchPanel : public Vr41xxPiuPanel {
public:
    using Vr41xxPiuPanel::Vr41xxPiuPanel;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::CasioToricomail;
    }

    std::optional<uint16_t> ConvertCommandPort(uint16_t adcmd,
                                               uint16_t /*pos_x*/,
                                               uint16_t /*pos_y*/) override {
        if (adcmd != kAdcmdTpx0) {
            LOG(Caution, "CasioToricomailTouchPanel: PIUCMDREG ADCMD=0x%X selects an A/D "
                    "port this board's panel does not drive\n", adcmd);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        return std::nullopt;
    }

    /* PIUCNTREG D5 PADSCANTYPE is "touch pressure sampling enable - 0: Prohibit" (VR4121 UM
       20.3.1); touch.dll sub_13715A0 never sets it, and sub_1370AB0 reads only PIUPBn0-3. */
    std::optional<uint16_t> PressureSample() override { return std::nullopt; }

    /* nk.exe sub_9F0B61DC scans ADIN0 (PIUAMSKREG 0xEF) as the main battery and sub_9F0B64C4
       scans ADIN1 (PIUAMSKREG 0xDF) as the second cell; gwes.exe sub_ABEEC / sub_AC38C consume
       them. rules.md forbids a backup-battery model, so ADIN1 reports a fixed healthy cell. */
    std::optional<uint16_t> AdPortScanSample(uint16_t port) override {
        switch (port) {
            case kAdcmdAdin0: {
                const int fill = emu_.Get<CasioToricomailBattery>().FillPercent();
                if (fill >= 66) return kMainFull;
                if (fill >= 33) return kMainMid;
                return kMainLow;
            }
            case kAdcmdAdin1: return kSecondCellHealthy;
            default:
                LOG(Caution, "CasioToricomailTouchPanel: ADPortScan A/D port 0x%X this board "
                        "does not drive\n", port);
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioToricomailTouchPanel, Vr41xxPiuPanel);
