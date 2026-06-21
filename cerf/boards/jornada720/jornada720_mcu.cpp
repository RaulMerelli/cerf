#include "../../socs/sa11xx/sa11xx_ssp_device.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../board_detector.h"
#include "jornada720_battery.h"
#include "jornada720_keyboard.h"
#include "jornada720_led.h"
#include "jornada720_touch.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace {

/* HP doc §4.2: both directions are bit-reversed within each byte ("to send
   0xCA, shuffle to 0x53"); the OAL's reverse helper is nk.exe sub_8004F2BC. */
uint8_t BitRev8(uint8_t v) {
    v = (uint8_t)((v >> 4) | (v << 4));
    v = (uint8_t)(((v & 0xCCu) >> 2) | ((v & 0x33u) << 2));
    return (uint8_t)(((v & 0xAAu) >> 1) | ((v & 0x55u) << 1));
}

/* Jornada 720 keyboard/touch/battery/PWM MCU (HP doc §4, nk.exe
   sub_8004F244). Wire format: TX byte arrives bit-reversed in frame bits
   15:8, the response must go back bit-reversed in bits 7:0 - decoding the
   low byte or skipping BitRev8 re-hangs the OAL's SSSR.RNE poll. */
class Jornada720Mcu : public Sa11xxSspDevice {
public:
    using Sa11xxSspDevice::Sa11xxSspDevice;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Jornada720;
    }

    uint16_t Exchange(uint16_t tx_frame) override {
        const uint8_t byte = BitRev8((uint8_t)(tx_frame >> 8));
        return BitRev8(Consume(byte));
    }

private:
    /* HP doc §4.7 command values (+ the 0xD5/0xD7 setters this ROM's OAL
       actually uses - sub_8004F454 sends D5/D7, level, 0x3F trailer). */
    enum : uint8_t {
        kTxDummy        = 0x11,
        /* The OAL sends the read-clock dummy as raw wire 0x11 (not bit-reversed
           like commands), so it arrives here as BitRev8(0x11)=0x88. */
        kTxDummyRaw     = 0x88,
        kGetScanKeyCode = 0x90,
        kKbdInitParam   = 0x95,
        kGetTouchSamples= 0xA0,
        kGetBatteryData = 0xC0,
        kGetContrast    = 0xD0,
        kSetContrast    = 0xD1,
        kGetBrightness  = 0xD2,
        kSetBrightness  = 0xD3,
        kSetContrast2   = 0xD5,
        kSetBrightness2 = 0xD7,
        kContrastOff    = 0xD8,
        kBrightnessOff  = 0xD9,
        kPwmOff         = 0xDF,
        /* Notification LED (gwes.exe nled driver sub_926CC / sub_927C4). */
        kGetLedStatus   = 0xE0,
        kLedOn          = 0xE1,
        kLedBlink       = 0xE2,
        kLedOff         = 0xE3,
    };

    /* battdrv.dll sub_ED1814 maps main-battery ADC to percent in 6 bands using
       the table @0xED1220 = {512,585,628,638,651,665}: <512 -> 0%, >=665 ->
       100%, else 100*band/6. Pick the upper half of each band so the driver's
       downward-hysteresis midpoint reduction (sub_ED1814) can't drop a bucket. */
    static uint16_t MainAdcForFill(int fill) {
        static const uint16_t kRep[7] = { 500, 560, 615, 635, 647, 660, 700 };
        if (fill < 0)   fill = 0;
        if (fill > 100) fill = 100;
        return kRep[(fill * 6 + 50) / 100];   /* band = round(fill*6/100) */
    }

    uint8_t Consume(uint8_t byte) {
        if (param_bytes_left_ > 0) {
            --param_bytes_left_;
            if (param_target_) {
                *param_target_ = byte;
                param_target_ = nullptr;
            }
            return kTxDummy;
        }
        if (!response_.empty()) {
            const uint8_t r = response_.front();
            response_.pop_front();
            return r;
        }
        if (byte == kTxDummy || byte == kTxDummyRaw) return kTxDummy;
        return Command(byte);
    }

    uint8_t Command(uint8_t cmd) {
        response_.clear();
        switch (cmd) {
            case kGetScanKeyCode: {
                /* §4.3: first TxDummy returns the scancode count, then the
                   codes; Jornada720Keyboard owns the GPIO0-fed queue. */
                std::vector<uint8_t> codes;
                emu_.Get<Jornada720Keyboard>().DrainScancodes(codes);
                response_.push_back((uint8_t)codes.size());
                for (uint8_t c : codes) response_.push_back(c);
                break;
            }
            case kGetTouchSamples: {
                /* touch.dll sub_FB1DD0 reads the 8 bytes as X1,X2,X3,Y1,Y2,Y3,
                   then a combined-X byte (s1/s2/s3 bits 8,9 at bit pairs 0/2/4)
                   and a combined-Y byte. Three identical samples per axis - the
                   driver's median filter (sub_FB1C38) collapses them. */
                uint16_t ax = 0, ay = 0;
                emu_.Get<Jornada720Touch>().CurrentAdc(&ax, &ay);
                const uint8_t xl = (uint8_t)(ax & 0xFF);
                const uint8_t yl = (uint8_t)(ay & 0xFF);
                const uint8_t xc = (uint8_t)((ax >> 8) & 3);
                const uint8_t yc = (uint8_t)((ay >> 8) & 3);
                response_.push_back(xl);
                response_.push_back(xl);
                response_.push_back(xl);
                response_.push_back(yl);
                response_.push_back(yl);
                response_.push_back(yl);
                response_.push_back((uint8_t)(xc | (xc << 2) | (xc << 4)));
                response_.push_back((uint8_t)(yc | (yc << 2) | (yc << 4)));
                break;
            }
            case kGetBatteryData: {
                /* §4.5: 3 bytes [main_low, backup_low, combined], each ADC
                   10-bit; combined bits 0,1 = main 8,9 and bits 2,3 = backup 8,9.
                   Main mapped from the widget fill via the battery driver's own
                   bands; backup = a healthy coin cell. */
                const uint16_t main_adc =
                    MainAdcForFill(emu_.Get<Jornada720Battery>().FillPercent());
                constexpr uint16_t kBackupAdc = 0x390;   /* >0x383 -> HIGH (battdrv.dll sub_ED1AC8). */
                response_.push_back((uint8_t)(main_adc & 0xFF));
                response_.push_back((uint8_t)(kBackupAdc & 0xFF));
                response_.push_back((uint8_t)(((main_adc >> 8) & 3) |
                                              (((kBackupAdc >> 8) & 3) << 2)));
                break;
            }
            case kGetContrast:   response_.push_back(contrast_);   break;
            case kGetBrightness: response_.push_back(brightness_); break;
            case kSetContrast:
                param_bytes_left_ = 1;
                param_target_ = &contrast_;
                break;
            case kSetBrightness:
                param_bytes_left_ = 1;
                param_target_ = &brightness_;
                break;
            case kKbdInitParam:
                /* Undocumented; TSCkbdr.dll sub_FC4248 sends 0x95 + 2 data
                   bytes at kbd-driver init. Consume them so they aren't
                   mis-parsed as commands; no observable readback. */
                param_bytes_left_ = 2;
                break;
            case kSetContrast2:               /* level + 0x3F trailer. */
                param_bytes_left_ = 2;
                param_target_ = &contrast_;
                break;
            case kSetBrightness2:
                param_bytes_left_ = 2;
                param_target_ = &brightness_;
                break;
            case kGetLedStatus:
                response_.push_back(emu_.Get<Jornada720Led>().StatusByte());
                break;
            case kLedOn:
                emu_.Get<Jornada720Led>().SetState(Jornada720Led::State::On);
                break;
            case kLedBlink:
                emu_.Get<Jornada720Led>().SetState(Jornada720Led::State::Blink);
                break;
            case kLedOff:
                emu_.Get<Jornada720Led>().SetState(Jornada720Led::State::Off);
                break;
            case kContrastOff:
            case kBrightnessOff:
            case kPwmOff:
                break;                        /* PWM gating: no CERF effect. */
            default:
                LOG(Caution, "Jornada720Mcu: unknown command 0x%02X - "
                    "answering TxDummy\n", cmd);
                break;
        }
#if CERF_DEV_MODE
        LOG(Periph, "[J720MCU] cmd 0x%02X (queued %zu response bytes, "
            "%u params expected)\n", cmd, response_.size(),
            param_bytes_left_);
#endif
        return kTxDummy;
    }

    std::deque<uint8_t> response_;
    uint32_t param_bytes_left_ = 0;
    uint8_t* param_target_     = nullptr;
    uint8_t  contrast_   = 0x36;   /* firmware defaults (sub_8004F454 /  */
    uint8_t  brightness_ = 0x19;   /* HP doc §5 display-on sequence).    */
};

}  /* namespace */

REGISTER_SERVICE_AS(Jornada720Mcu, Sa11xxSspDevice);
