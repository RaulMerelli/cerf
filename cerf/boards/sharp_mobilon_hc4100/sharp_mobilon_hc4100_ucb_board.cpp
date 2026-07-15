#include "../../peripherals/philips_ucb1200/ucb1x00_board.h"

#include "sharp_mobilon_hc4100_battery.h"
#include "sharp_mobilon_hc4100_touch_panel.h"
#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"

#include <cstdint>

namespace {

[[noreturn]] void Unwired(const char* what) {
    LOG(Caution, "SharpMobilonHc4100UcbBoard: %s not yet grounded\n", what);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

uint16_t Clamp10(int v) { return static_cast<uint16_t>(v < 0 ? 0 : (v > 1023 ? 1023 : v)); }

/* touch.dll sub_14A1170 feeds the raw ADC coordinate through TouchPanelCalibrateAPoint,
   so X/Y are 10-bit counts under an affine calibration; step 1 spans the 640-px width
   without clamping past 1023. */
uint16_t PixelToAdc(int px) { return Clamp10(64 + (px < 0 ? 0 : px)); }

/* UCB touch pressure convention (as in the sibling ucb boards): full scale under the pen,
   zero released, crossing the driver's pen-detect threshold. */
constexpr uint16_t kPressureDown = 0x3FF;
constexpr uint16_t kPressureUp   = 0u;

/* gwes.exe battery poll sub_9F38C reads AD0/AD2 as the main battery and AD3 as the
   backup; the flag classifier sub_9F0E4 scales raw as 75*raw/1024 and (battery type
   0, threshold table dword_156D0[0]={22,20}) reports HIGH above 22 (raw>=315) and
   CRITICAL at/below 20 (raw<=286). Span fill 0..100% from a CRITICAL to a HIGH raw. */
constexpr int      kMainAdcEmpty     = 0x100;   /* 256 -> CRITICAL */
constexpr int      kMainAdcFull      = 0x200;   /* 512 -> HIGH */
constexpr uint16_t kBackupAdcHealthy = 0x200;   /* backup cell always fresh (HIGH) */

uint16_t MainBatteryAdc(int fill_percent) {
    if (fill_percent < 0)   fill_percent = 0;
    if (fill_percent > 100) fill_percent = 100;
    return static_cast<uint16_t>(
        kMainAdcEmpty + fill_percent * (kMainAdcFull - kMainAdcEmpty) / 100);
}

class SharpMobilonHc4100UcbBoard : public Ucb1x00Board {
public:
    using Ucb1x00Board::Ucb1x00Board;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SharpMobilonHc4100;
    }

    uint16_t AuxAdc(uint8_t channel) override {
        switch (channel) {
            case 0:
            case 2:
                return MainBatteryAdc(emu_.Get<SharpMobilonHc4100Battery>().FillPercent());
            case 3:
                return kBackupAdcHealthy;
            default:
                Unwired("AuxAdc");
        }
    }
    uint16_t TouchAdcX() override { return PixelToAdc(emu_.Get<SharpMobilonHc4100TouchPanel>().X()); }
    uint16_t TouchAdcY() override { return PixelToAdc(emu_.Get<SharpMobilonHc4100TouchPanel>().Y()); }
    uint16_t TouchAdcPressure() override {
        return emu_.Get<SharpMobilonHc4100TouchPanel>().Down() ? kPressureDown : kPressureUp;
    }
    /* sib.dll reads IO_DATA back after driving it (SF0AUX reg 0, DMA engine
       sub_154163C @0x1541A18); output pins read back their driven level. */
    uint16_t IoData(uint16_t driven) override { return driven; }

    bool TouchDown() const override {
        return emu_.Get<SharpMobilonHc4100TouchPanel>().Down();
    }
    uint16_t PenIrqStatus() override {
        return emu_.Get<SharpMobilonHc4100TouchPanel>().PenIrqStatus();
    }
    void ClearPenIrq(uint16_t mask) override {
        emu_.Get<SharpMobilonHc4100TouchPanel>().ClearPenIrq(mask);
    }
    void SetPenIrqArmed(uint16_t armed_bits) override {
        emu_.Get<SharpMobilonHc4100TouchPanel>().SetPenIrqArmed(armed_bits);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(SharpMobilonHc4100UcbBoard, Ucb1x00Board);
