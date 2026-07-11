#include "../../peripherals/philips_ucb1200/ucb1x00_board.h"

#include "philips_velo_1_battery.h"
#include "philips_velo_1_touch_panel.h"
#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"

#include <cstdint>

namespace {

/* gwes.exe sub_739B0 reads the UCB aux ADC over SIB1: IOCTL 1 (serial.dll
   sub_1EB9200): a3==2 -> INP AD2 -> main gauge (OAL globals+40), a3==3 -> INP AD3
   -> backup coin cell (globals+54). ADC_CR INP 4..7 maps to AuxAdc channel 0..3,
   so AD2 is channel 2 and AD3 is channel 3. */
constexpr uint8_t kAuxMain   = 2;   /* AD2 */
constexpr uint8_t kAuxBackup = 3;   /* AD3 */

/* sub_739B0 bands the averaged main raw: <0x89 no gauge, >=0x141 HIGH, >=0x12D
   MED, else LOW. Empty sits at the 0x89 valid floor (reads LOW), full above 0x141. */
constexpr int kMainAdcEmpty = 0x89;
constexpr int kMainAdcFull  = 0x200;

/* sub_739B0 bands the backup coin cell >=0x16F HIGH, >=0x13C MED, >=0xD4 LOW; the
   emulated cell is always fresh. */
constexpr uint16_t kBackupAdcHealthy = 0x200;

uint16_t MainBatteryAdc(int fill_percent) {
    if (fill_percent < 0)   fill_percent = 0;
    if (fill_percent > 100) fill_percent = 100;
    const int raw = kMainAdcEmpty + fill_percent * (kMainAdcFull - kMainAdcEmpty) / 100;
    return static_cast<uint16_t>(raw);
}

uint16_t Clamp10(int v) { return static_cast<uint16_t>(v < 0 ? 0 : (v > 1023 ? 1023 : v)); }

/* touch.dll (sub_1F211C0 -> TouchPanelCalibrateAPoint) calibrates any monotonic
   pixel->raw map, but the 10-bit ADC caps it: base + slope*max_coord must stay <= 1023,
   and X runs to 479 on the 480x240 panel, so slope is 2 (64 + 479*2 = 1022). */
uint16_t PixelToAdc(int px) { return Clamp10(64 + (px < 0 ? 0 : px) * 2); }

constexpr uint16_t kPressureDown = 0x3FF;
constexpr uint16_t kPressureUp   = 0u;

class PhilipsVelo1UcbBoard : public Ucb1x00Board {
public:
    using Ucb1x00Board::Ucb1x00Board;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::PhilipsVelo1;
    }

    uint16_t AuxAdc(uint8_t channel) override {
        switch (channel) {
            case kAuxMain:   return MainBatteryAdc(emu_.Get<PhilipsVelo1Battery>().FillPercent());
            case kAuxBackup: return kBackupAdcHealthy;
            default:
                LOG(Caution, "PhilipsVelo1UcbBoard: aux ADC channel %u unmodeled\n", channel);
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
    }

    bool TouchDown() const override { return emu_.Get<PhilipsVelo1TouchPanel>().Down(); }
    uint16_t TouchAdcX() override { return PixelToAdc(emu_.Get<PhilipsVelo1TouchPanel>().X()); }
    uint16_t TouchAdcY() override { return PixelToAdc(emu_.Get<PhilipsVelo1TouchPanel>().Y()); }
    uint16_t TouchAdcPressure() override {
        return emu_.Get<PhilipsVelo1TouchPanel>().Down() ? kPressureDown : kPressureUp;
    }

    uint16_t IoData(uint16_t) override {
        LOG(Caution, "PhilipsVelo1UcbBoard: codec I/O pins unmodeled\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    uint16_t PenIrqStatus() override { return emu_.Get<PhilipsVelo1TouchPanel>().PenIrqStatus(); }
    void ClearPenIrq(uint16_t mask) override { emu_.Get<PhilipsVelo1TouchPanel>().ClearPenIrq(mask); }
    void SetPenIrqArmed(uint16_t bits) override {
        emu_.Get<PhilipsVelo1TouchPanel>().SetPenIrqArmed(bits);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(PhilipsVelo1UcbBoard, Ucb1x00Board);
