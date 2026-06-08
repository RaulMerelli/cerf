#include "../../socs/sa11xx/sa11xx_ssp_device.h"

#include "../../core/cerf_emulator.h"
#include "../board_detector.h"

#include <cstdint>
#include <deque>

namespace {

/* HP Jornada 820 keyboard controller on the SA-1100 SSP (keybddr.dll): the
   driver sends ESC commands { 0x1B, cmd, params } one byte per frame, then
   clocks the reply { 0x80 marker, cmd-ack, data } with dummy 0x00 frames
   (keybddr sub_12C19AC). Init cmd 0xA0 -> ack 0xA1; cmd 0xA9 = 16-byte config. */
class Jornada820Keyboard : public Sa11xxSspDevice {
public:
    using Sa11xxSspDevice::Sa11xxSspDevice;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Jornada820;
    }

    uint16_t Exchange(uint16_t tx_frame) override {
        const uint8_t b = static_cast<uint8_t>(tx_frame & 0xFFu);

        if (b == 0x1Bu) {              /* ESC always begins a fresh command */
            reply_.clear();
            cmd_ = -1;
            params_left_ = 0;
            in_cmd_ = true;
            return 0;
        }
        if (!reply_.empty()) {         /* driver is clocking out the reply */
            const uint8_t r = reply_.front();
            reply_.pop_front();
            return r;
        }
        if (in_cmd_) {
            if (cmd_ < 0) {            /* the command byte after ESC */
                cmd_ = b;
                params_left_ = ParamCount(b);
                if (params_left_ == 0) FinishCommand();
            } else if (params_left_ > 0 && --params_left_ == 0) {
                FinishCommand();
            }
            return 0;
        }
        return 0;                      /* idle: not 0x80, so reads see "no key" */
    }

private:
    static int ParamCount(uint8_t cmd) {
        switch (cmd) {
            case 0xA0: return 1;       /* { ESC, A0, 7B } */
            case 0xA9: return 16;      /* { ESC, A9, 16 bytes } */
            default:   return 0;
        }
    }

    void FinishCommand() {
        if (cmd_ == 0xA0) {
            reply_.assign({0x80u, 0xA1u, 0x00u});  /* marker, ack, discard */
        }
        in_cmd_ = false;
        cmd_ = -1;
    }

    std::deque<uint8_t> reply_;
    int  cmd_         = -1;
    int  params_left_ = 0;
    bool in_cmd_      = false;
};

}  /* namespace */

REGISTER_SERVICE_AS(Jornada820Keyboard, Sa11xxSspDevice);
