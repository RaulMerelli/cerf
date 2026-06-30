#define NOMINMAX

#include "../../host/touch_input.h"

#include <windows.h>

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../state/emulation_freeze.h"
#include "../board_context.h"
#include "nec_mobilepro_900_pco_companion.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

namespace {

/* The PIC streams 0x04 position continuously while the pen is down (Linux mp900
   driver). The pacer re-emits the held position so a motionless hold keeps
   feeding pco; without it touch.dll's slow internal poll makes gwes read quick
   taps as long holds (context menu) and breaks double-tap. */
class NecMobilePro900TouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    ~NecMobilePro900TouchInput() override {
        shutdown_.store(true, std::memory_order_release);
        if (wake_) SetEvent(wake_);
        if (pacer_.joinable()) pacer_.join();
        if (wake_) CloseHandle(wake_);
    }

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::NecMobilePro900;
    }

    void OnReady() override {
        wake_  = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        pacer_ = std::thread([this] { PacerMain(); });
    }

    void OnPenDown(int x, int y) override {
        LOG(Board, "[NEC-TOUCH] OnPenDown %d,%d\n", x, y);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            down_ = true;
            x_ = static_cast<uint16_t>(x);
            y_ = static_cast<uint16_t>(y);
        }
        SetEvent(wake_);
    }

    void OnPenMove(int x, int y) override {
        std::lock_guard<std::mutex> lk(mtx_);
        x_ = static_cast<uint16_t>(x);
        y_ = static_cast<uint16_t>(y);
    }

    void OnPenUp(int, int) override {
        LOG(Board, "[NEC-TOUCH] OnPenUp\n");
        {
            std::lock_guard<std::mutex> lk(mtx_);
            down_ = false;
            up_pending_ = true;
        }
        SetEvent(wake_);
    }

    void OnCaptureLost() override { OnPenUp(0, 0); }

private:
    /* Stream period while down. The driver does not fix the PIC scan rate, so
       this is sized to stay faster than touch.dll's default 16.7 ms acquire
       poll (rate 300) - the [TOUCH-RATE] hook reports the live rate to confirm. */
    static constexpr DWORD kStreamPeriodMs = 15u;

    void PacerMain() {
        auto& freeze = emu_.Get<EmulationFreeze>();
        auto& pco    = emu_.Get<NecMobilePro900PcoCompanion>();
        while (!shutdown_.load(std::memory_order_acquire)) {
            bool down, up_pending;
            uint16_t x, y;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                down = down_;
                up_pending = up_pending_;
                up_pending_ = false;
                x = x_;
                y = y_;
            }
            if (down) {
                auto frozen = freeze.WorkerSection();
                pco.SendTouch(x, y);
            } else if (up_pending) {
                auto frozen = freeze.WorkerSection();
                pco.SendPenUp();
            }
            WaitForSingleObject(wake_, down ? kStreamPeriodMs : INFINITE);
        }
    }

    std::mutex        mtx_;
    bool              down_ = false;
    bool              up_pending_ = false;
    uint16_t          x_ = 0, y_ = 0;
    std::atomic<bool> shutdown_{false};
    HANDLE            wake_ = nullptr;
    std::thread       pacer_;
};

}  /* namespace */

REGISTER_SERVICE_AS(NecMobilePro900TouchInput, TouchInput);
