#include "../../peripherals/philips_ucb1200/ucb1x00_board.h"

#include "philips_nino_300_battery.h"
#include "philips_nino_300_touch_panel.h"
#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"

#include <cstdint>

namespace {

[[noreturn]] void Unwired(const char* what) {
    LOG(Caution, "PhilipsNino300UcbBoard: %s is not wired to the codec\n", what);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

/* gwes.exe (sub_82C18) reads the UCB1200 auxiliary ADC channels over SIB1: IOCTL
   1, whose in-buffer LENGTH is the channel index: AD0 the main battery, AD2 the
   backup cell, AD3 a battery-type sense, each a raw 10-bit count. ADC_CR INP 4..7
   maps to AuxAdc channel 0..3, so AD0 is channel 0. */
constexpr uint8_t kAuxMain      = 0;   /* AD0 */
constexpr uint8_t kAuxBackup    = 2;   /* AD2 */
constexpr uint8_t kAuxChemistry = 3;   /* AD3 */

/* gwes.exe sub_83088 flags the main battery HIGH at raw > MainLow (0x133) and
   CRITICAL at raw <= MainCritical (0x11E) - ControlPanel\Battery\Alkaline, the set
   AD3 selects - and never fills BatteryLifePercent, so only the flag shows.
   kMainAdcFull must stay above MainNotLow (0x141) or a full battery reads LOW. */
constexpr int kMainAdcEmpty = 0x100;
constexpr int kMainAdcFull  = 0x200;

/* gwes.exe sub_82FE4 reports the AD2 backup cell HIGH at raw >= 0x140; the
   emulated coin cell is always fresh. */
constexpr uint16_t kBackupAdcHealthy = 0x200;

/* gwes.exe sub_82C18 selects the Alkaline threshold set when AD3 < 0x104, else
   NiMH; the Nino ships two AA alkaline cells. */
constexpr uint16_t kChemistryAlkalineAdc = 0x80;

/* touch.dll's sampler thresholds the pen-mode reading (sub_1891B84 vs word_18B5960);
   return full scale while down and zero while up so any threshold registers. */
constexpr uint16_t kPressureDown = 0x3FF;
constexpr uint16_t kPressureUp   = 0u;

uint16_t Clamp10(int v) { return static_cast<uint16_t>(v < 0 ? 0 : (v > 1023 ? 1023 : v)); }

/* The wizard fits an affine map from CERF's raw samples to pixels
   (TouchPanelSetCalibration -> TouchPanelCalibrateAPoint), so any monotonic
   linear pixel->raw response calibrates correctly; the base/step are free. */
uint16_t PixelToAdc(int px) { return Clamp10(64 + (px < 0 ? 0 : px) * 3); }

uint16_t MainBatteryAdc(int fill_percent) {
    if (fill_percent < 0)   fill_percent = 0;
    if (fill_percent > 100) fill_percent = 100;
    const int raw = kMainAdcEmpty +
                    fill_percent * (kMainAdcFull - kMainAdcEmpty) / 100;
    return static_cast<uint16_t>(raw);
}

class PhilipsNino300UcbBoard : public Ucb1x00Board {
public:
    using Ucb1x00Board::Ucb1x00Board;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::PhilipsNino300;
    }

    uint16_t AuxAdc(uint8_t channel) override {
        switch (channel) {
            case kAuxMain:
                return MainBatteryAdc(emu_.Get<PhilipsNino300Battery>().FillPercent());
            case kAuxBackup:    return kBackupAdcHealthy;
            case kAuxChemistry: return kChemistryAlkalineAdc;
            default:            Unwired("an auxiliary ADC channel");
        }
    }

    bool     TouchDown() const override { return emu_.Get<PhilipsNino300TouchPanel>().Down(); }
    uint16_t TouchAdcX() override { return PixelToAdc(emu_.Get<PhilipsNino300TouchPanel>().X()); }
    uint16_t TouchAdcY() override { return PixelToAdc(emu_.Get<PhilipsNino300TouchPanel>().Y()); }
    uint16_t TouchAdcPressure() override {
        return emu_.Get<PhilipsNino300TouchPanel>().Down() ? kPressureDown : kPressureUp;
    }

    /* sib.dll sub_18D1654 writes IO_DIR = 0x83FF, driving all ten I/O ports
       (ucb1200reg.h:56 UCB1200_IOPORT_MAX 10), so the port reads back what the
       driver drives. sub_18D14E0 idles a subframe with a read of this register. */
    uint16_t IoData(uint16_t driven) override { return driven; }

    uint16_t PenIrqStatus() override { return emu_.Get<PhilipsNino300TouchPanel>().PenIrqStatus(); }
    void ClearPenIrq(uint16_t mask) override { emu_.Get<PhilipsNino300TouchPanel>().ClearPenIrq(mask); }
    void SetPenIrqArmed(uint16_t bits) override {
        emu_.Get<PhilipsNino300TouchPanel>().SetPenIrqArmed(bits);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(PhilipsNino300UcbBoard, Ucb1x00Board);
