#include "../../socs/sa11xx/sa11xx_mcp_codec.h"

#include "simpad_sl4_touch_panel.h"
#include "simpad_sl4_battery.h"
#include "simpad_sl4_keypad.h"
#include "../board_detector.h"
#include "../../core/cerf_emulator.h"

#include <array>
#include <cstdint>

namespace {


/* UCB register indices + bit fields, Linux ucb1x00.h. */
constexpr uint8_t  kRegIoData  = 0x00;
constexpr uint8_t  kRegIeFal   = 0x03;   /* IE falling-edge enable */
constexpr uint8_t  kRegIeStatus = 0x04;  /* IE status (read) / IE clear (write) */
constexpr uint8_t  kRegTsCr    = 0x09;
constexpr uint16_t kUcbIeTspx  = 1u << 12;   /* UCB_IE_TSPX pen-detect */
constexpr uint8_t  kRegAdcCr   = 0x0A;
constexpr uint8_t  kRegAdcData = 0x0B;
constexpr uint8_t  kRegId      = 0x0C;

constexpr uint16_t kIdUcb1300   = 0x1005;        /* UCB_ID_1300 */
constexpr uint16_t kTsCrModeMask = (3u << 8);    /* MODE [9:8] */
constexpr uint16_t kTsCrModePres = (1u << 8);    /* UCB_TS_CR_MODE_PRES */
constexpr uint16_t kTsCrTspxLow  = (1u << 12);   /* pen-detect bits */
constexpr uint16_t kTsCrTsmxLow  = (1u << 13);
constexpr uint16_t kAdcStart   = (1u << 7);      /* UCB_ADC_START */
constexpr uint16_t kAdcDatVal  = (1u << 15);     /* UCB_ADC_DAT_VAL */
constexpr uint16_t kAdcInpMask = (7u << 2);      /* INP [4:2] */
constexpr uint16_t kAdcInpTspx = (0u << 2);      /* Y position plate (Linux read_ypos) */
constexpr uint16_t kAdcInpTspy = (2u << 2);      /* X position plate (Linux read_xpos) */
constexpr uint16_t kAdcInpAd1  = (5u << 2);      /* aux AD1 = battery voltage (gwes sub_AB788(1)) */
constexpr uint16_t kAdcInpAd2  = (6u << 2);      /* aux AD2 = AC-line sense (gwes sub_AB788(2)) */

/* touch.dll (tchpdd sub_13119DC) treats pressure-mode ADC >= 290 as pen-down. */
constexpr uint16_t kPressureDown = 512u;
constexpr uint16_t kPressureUp   = 0u;

/* gwes (sub_ABA64) reads AD2 as the AC-line sense; its divide makes
   adc >= 289 register as "AC online" - 400 when on AC, 0 on battery. */
constexpr uint16_t kAcOnlineAdc = 400u;

class SimpadSl4Ucb1300 : public Sa11xxMcpCodec {
public:
    using Sa11xxMcpCodec::Sa11xxMcpCodec;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::SimpadSl4;
    }

    uint16_t ReadReg(uint8_t reg) override {
        switch (reg) {
            case kRegIoData:    return KeypadIoData();
            case kRegIeStatus:  return emu_.Get<SimpadSl4TouchPanel>().PenIrqStatus();
            case kRegId:        return kIdUcb1300;
            case kRegAdcData:   return adc_data_;
            case kRegTsCr:      return PenDetectTsCr();
            default:            return regs_[reg & 0xFu];
        }
    }

    void WriteReg(uint8_t reg, uint16_t value) override {
        regs_[reg & 0xFu] = value;
        if (reg == kRegAdcCr && (value & kAdcStart)) Convert(value);
        else if (reg == kRegIeFal)
            emu_.Get<SimpadSl4TouchPanel>().SetPenIrqArmed((value & kUcbIeTspx) != 0);
        else if (reg == kRegIeStatus)
            emu_.Get<SimpadSl4TouchPanel>().ClearPenIrq(value);
    }

private:
    uint16_t KeypadIoData() const {
        /* IO_DATA bits 0..5 are the keypad button lines (active-low); other IO
           bits keep whatever the driver last drove. */
        const uint8_t buttons = emu_.Get<SimpadSl4Keypad>().ReleasedIoBits();
        return static_cast<uint16_t>((regs_[kRegIoData] & ~0x3Fu) | buttons);
    }

    uint16_t PenDetectTsCr() const {
        uint16_t v = regs_[kRegTsCr];
        if (emu_.Get<SimpadSl4TouchPanel>().Down()) v |=  (kTsCrTspxLow | kTsCrTsmxLow);
        else                                        v &= ~(kTsCrTspxLow | kTsCrTsmxLow);
        return v;
    }

    void Convert(uint16_t adc_cr) {
        auto& panel = emu_.Get<SimpadSl4TouchPanel>();
        const bool     down = panel.Down();
        const uint16_t mode = regs_[kRegTsCr] & kTsCrModeMask;
        const uint16_t chan = adc_cr & kAdcInpMask;
        auto& batt = emu_.Get<SimpadSl4Battery>();
        uint16_t s;
        if (chan == kAdcInpAd1)         s = BatteryAdc(batt.FillPercent());  /* aux channel, TS mode N/A */
        else if (chan == kAdcInpAd2)    s = batt.IsOnBattery() ? 0u : kAcOnlineAdc;
        else if (mode == kTsCrModePres) s = down ? kPressureDown : kPressureUp;
        else if (chan == kAdcInpTspy)   s = AdcX(panel.X());
        else if (chan == kAdcInpTspx)   s = AdcY(panel.Y());
        else                            s = 0u;                /* aux AD0/AD3: unconnected */
        adc_data_ = static_cast<uint16_t>(kAdcDatVal | ((s & 0x3FFu) << 5));  /* UCB_ADC_DAT layout */
    }

    /* Inverse of touch.dll's transform (sub_1311F40 / sub_1312654):
       X_pixel = (raw>>1 - 48) * 47/16  ->  raw = hx*32/47 + 96;
       Y_pixel = (raw>>1 - 80) * 37/16  ->  raw = hy*32/37 + 160. */
    /* gwes battery monitor (gwes.exe sub_ABA64) reads AD1 as mV=12600*(adc+21)/860+170
       and shows 0..100% across [7200,8200] (this pack's range; gwes Version word 83,
       runtime-confirmed), suspending below 7200. Map the widget's 0..100% across that
       range; a lower low end would spuriously suspend at mid widget levels. */
    static uint16_t BatteryAdc(int pct) {
        if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
        const int mv = 7200 + pct * 10;
        return Clamp10((mv - 170) * 860 / 12600 - 21);
    }

    static uint16_t AdcX(int hx) { return Clamp10((hx < 0 ? 0 : hx) * 32 / 47 + 96); }
    static uint16_t AdcY(int hy) { return Clamp10((hy < 0 ? 0 : hy) * 32 / 37 + 160); }
    static uint16_t Clamp10(int v) { return static_cast<uint16_t>(v < 0 ? 0 : (v > 1023 ? 1023 : v)); }

    std::array<uint16_t, 16> regs_{};
    uint16_t adc_data_ = 0;
};

}  /* namespace */

REGISTER_SERVICE_AS(SimpadSl4Ucb1300, Sa11xxMcpCodec);
