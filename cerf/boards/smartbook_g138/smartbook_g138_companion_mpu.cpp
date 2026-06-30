#include "../../core/service.h"

#include "../../core/cerf_emulator.h"
#include "../board_context.h"
#include "../../host/host_canvas.h"
#include "../../host/keyboard_input.h"
#include "../../host/keyboard_map.h"
#include "../../host/keyboard_router.h"
#include "../../host/touch_input.h"
#include "../../socs/sa11xx/sa11xx_sp3_uart.h"
#include "../../state/emulation_freeze.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace {

/* G138 companion MPU on SP3 UART ("COM3"). kbdmouse.dll demuxes RX frames by
   leading code: 0xBF/0xDF (5-byte) -> touch/penmount.dll (sub_1B52CDC, sub_1B43070);
   0xFA/0xF0 (2-byte) -> keyboard make/break (sub_1B526BC). The byte layouts below
   must match those parsers exactly or the guest silently drops the input. */
class SmartBookG138CompanionMpu : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SmartBookG138;
    }

    void SendTouch(bool contact, uint16_t x14, uint16_t y14) {
        const uint8_t pkt[5] = {
            static_cast<uint8_t>(contact ? 0xBFu : 0xDFu),
            static_cast<uint8_t>((x14 >> 7) & 0x7Fu),
            static_cast<uint8_t>(x14 & 0x7Fu),
            static_cast<uint8_t>((y14 >> 7) & 0x7Fu),
            static_cast<uint8_t>(y14 & 0x7Fu),
        };
        emu_.Get<Sa11xxSp3Uart>().PushRxBurst(pkt, sizeof(pkt));
    }

    /* Keyboard make/break: kbdmouse.dll sub_1B526BC reads the 2-byte frame as a
       little-endian word - low byte 0xFA=make / 0xF0=break, high byte = scancode. */
    void SendKey(uint8_t scancode, bool down) {
        const uint8_t pkt[2] = { static_cast<uint8_t>(down ? 0xFAu : 0xF0u), scancode };
        emu_.Get<Sa11xxSp3Uart>().PushRxBurst(pkt, sizeof(pkt));
    }
};

/* penmount.dll sub_1B43070 maps raw14*8 to a 0..0x7FFF mouse_event ABSOLUTE value
   linearly: norm = 26214*(raw8-caL)/(caH-caL)+3276, (caL,caH)=registry cax/cay (no
   cal-point table, so this path). Inverted to place pixel p over span S: the
   ABSOLUTE value is p*65535/(S-1) = 2*norm, so norm = p*32768/(S-1). */
constexpr int kCaxL = 0x6F40, kCaxH = 0x11C0;   /* registry cax1, cax2. */
constexpr int kCayL = 0x6AC0, kCayH = 0x1660;   /* registry cay1, cay2. */

static uint16_t RawFromPixel(int p, int span, int caL, int caH) {
    if (span < 2) span = 2;
    if (p < 0) p = 0;
    if (p > span - 1) p = span - 1;
    const int norm = p * 32768 / (span - 1);
    int raw8  = caL + (norm - 3276) * (caH - caL) / 26214;
    int c14   = raw8 / 8;
    if (c14 < 0) c14 = 0;
    if (c14 > 0x3FFF) c14 = 0x3FFF;
    return static_cast<uint16_t>(c14);
}

class SmartBookG138Touch : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SmartBookG138;
    }

    void OnReady() override {
        sampler_ = std::thread([this] { SamplerLoop(); });
    }

    ~SmartBookG138Touch() override {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
        if (sampler_.joinable()) sampler_.join();
    }

    void OnPenDown(int x, int y) override {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            last_x_ = x; last_y_ = y; pen_down_ = true;
        }
        cv_.notify_all();
    }
    void OnPenMove(int x, int y) override {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!pen_down_) return;
        last_x_ = x; last_y_ = y;
    }
    void OnPenUp(int /*x*/, int /*y*/) override {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (!pen_down_) return;
            pen_down_ = false;
        }
        cv_.notify_all();
    }
    void OnCaptureLost() override { OnPenUp(0, 0); }

private:
    std::mutex              mtx_;
    std::condition_variable cv_;
    std::thread             sampler_;
    bool                    stop_     = false;
    bool                    pen_down_ = false;
    int                     last_x_   = 0;
    int                     last_y_   = 0;

    void EmitContact(int x_host, int y_host) {
        auto&     hc = emu_.Get<HostCanvas>();
        const int w  = static_cast<int>(hc.GuestSurfaceWidth());
        const int h  = static_cast<int>(hc.GuestSurfaceHeight());
        const uint16_t x14 = RawFromPixel(x_host, w, kCaxL, kCaxH);
        const uint16_t y14 = RawFromPixel(y_host, h, kCayL, kCayH);
        emu_.Get<SmartBookG138CompanionMpu>().SendTouch(true, x14, y14);
    }

    void SamplerLoop() {
        auto& freeze = emu_.Get<EmulationFreeze>();
        std::unique_lock<std::mutex> lk(mtx_);
        bool was_down = false;
        while (!stop_) {
            if (pen_down_) {
                const int x = last_x_, y = last_y_;
                lk.unlock();
                { auto frozen = freeze.WorkerSection(); EmitContact(x, y); }
                lk.lock();
                was_down = true;
                cv_.wait_for(lk, std::chrono::milliseconds(20),
                             [&] { return stop_ || !pen_down_; });
            } else {
                if (was_down) {
                    was_down = false;
                    const int x = last_x_, y = last_y_;
                    lk.unlock();
                    {
                        auto& hc = emu_.Get<HostCanvas>();
                        const uint16_t x14 = RawFromPixel(
                            x, static_cast<int>(hc.GuestSurfaceWidth()), kCaxL, kCaxH);
                        const uint16_t y14 = RawFromPixel(
                            y, static_cast<int>(hc.GuestSurfaceHeight()), kCayL, kCayH);
                        auto frozen = freeze.WorkerSection();
                        emu_.Get<SmartBookG138CompanionMpu>().SendTouch(false, x14, y14);
                    }
                    lk.lock();
                }
                cv_.wait(lk, [&] { return stop_ || pen_down_; });
            }
        }
    }
};

class SmartBookG138KeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SmartBookG138;
    }

    void OnReady() override { emu_.Get<KeyboardRouter>().Register(this); }
    std::wstring SourceName() const override { return L"SmartBook keyboard"; }

    void OnHostKey(uint8_t vk, bool key_up) override {
        uint32_t code;
        if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code)) return;
        emu_.Get<SmartBookG138CompanionMpu>().SendKey(
            static_cast<uint8_t>(code), !key_up);
    }
};

}  /* namespace */

REGISTER_SERVICE(SmartBookG138CompanionMpu);
REGISTER_SERVICE_AS(SmartBookG138Touch, TouchInput);
REGISTER_SERVICE(SmartBookG138KeyboardInput);
