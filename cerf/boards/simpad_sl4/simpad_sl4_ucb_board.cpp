#include "../../peripherals/philips_ucb1200/ucb1x00_board.h"

#include "simpad_sl4_battery.h"
#include "simpad_sl4_keypad.h"
#include "simpad_sl4_touch_panel.h"
#include "../board_context.h"
#include "../../core/cerf_emulator.h"

#include <cstdint>

namespace {

/* touch.dll (tchpdd sub_13119DC) treats pressure-mode ADC >= 290 as pen-down. */
constexpr uint16_t kPressureDown = 512u;
constexpr uint16_t kPressureUp   = 0u;

/* gwes (sub_ABA64) reads AD2 as the AC-line sense; its divide makes
   adc >= 289 register as "AC online" - 400 when on AC, 0 on battery. */
constexpr uint16_t kAcOnlineAdc = 400u;

constexpr uint8_t kAuxAd1 = 1;   /* battery voltage (gwes sub_AB788(1)) */
constexpr uint8_t kAuxAd2 = 2;   /* AC-line sense  (gwes sub_AB788(2)) */

constexpr uint16_t kKeypadIoMask = 0x3Fu;

uint16_t Clamp10(int v) { return static_cast<uint16_t>(v < 0 ? 0 : (v > 1023 ? 1023 : v)); }

/* Inverse of touch.dll's transform (sub_1311F40 / sub_1312654):
   X_pixel = (raw>>1 - 48) * 47/16  ->  raw = hx*32/47 + 96;
   Y_pixel = (raw>>1 - 80) * 37/16  ->  raw = hy*32/37 + 160. */
uint16_t AdcX(int hx) { return Clamp10((hx < 0 ? 0 : hx) * 32 / 47 + 96); }
uint16_t AdcY(int hy) { return Clamp10((hy < 0 ? 0 : hy) * 32 / 37 + 160); }

/* gwes battery monitor (gwes.exe sub_ABA64) reads AD1 as mV=12600*(adc+21)/860+170
   and shows 0..100% across [7200,8200] (this pack's range; gwes Version word 83,
   runtime-confirmed), suspending below 7200. Map the widget's 0..100% across that
   range; a lower low end would spuriously suspend at mid widget levels. */
uint16_t BatteryAdc(int pct) {
    if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
    const int mv = 7200 + pct * 10;
    return Clamp10((mv - 170) * 860 / 12600 - 21);
}

class SimpadSl4UcbBoard : public Ucb1x00Board {
public:
    using Ucb1x00Board::Ucb1x00Board;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SimpadSl4;
    }

    uint16_t AuxAdc(uint8_t channel) override {
        auto& batt = emu_.Get<SimpadSl4Battery>();
        if (channel == kAuxAd1) return BatteryAdc(batt.FillPercent());
        if (channel == kAuxAd2) return batt.IsOnBattery() ? 0u : kAcOnlineAdc;
        return 0u;   /* AD0 and AD3 are unconnected */
    }

    bool     TouchDown() const override { return emu_.Get<SimpadSl4TouchPanel>().Down(); }
    uint16_t TouchAdcX() override { return AdcX(emu_.Get<SimpadSl4TouchPanel>().X()); }
    uint16_t TouchAdcY() override { return AdcY(emu_.Get<SimpadSl4TouchPanel>().Y()); }
    uint16_t TouchAdcPressure() override {
        return emu_.Get<SimpadSl4TouchPanel>().Down() ? kPressureDown : kPressureUp;
    }

    /* IO_DATA bits 0..5 are the keypad button lines (active-low); other IO bits
       keep whatever the driver last drove. */
    uint16_t IoData(uint16_t driven) override {
        const uint8_t buttons = emu_.Get<SimpadSl4Keypad>().ReleasedIoBits();
        return static_cast<uint16_t>((driven & ~kKeypadIoMask) | buttons);
    }

    uint16_t PenIrqStatus() override { return emu_.Get<SimpadSl4TouchPanel>().PenIrqStatus(); }
    void ClearPenIrq(uint16_t mask) override { emu_.Get<SimpadSl4TouchPanel>().ClearPenIrq(mask); }
    void SetPenIrqArmed(uint16_t bits) override {
        emu_.Get<SimpadSl4TouchPanel>().SetPenIrqArmed(bits);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(SimpadSl4UcbBoard, Ucb1x00Board);
