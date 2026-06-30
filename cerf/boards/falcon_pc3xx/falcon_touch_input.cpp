#define NOMINMAX

#include "../../host/touch_input.h"

#include <windows.h>

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/pxa255/pxa255_ac97.h"
#include "../../state/emulation_freeze.h"
#include "../board_context.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <thread>

namespace {

/* WM9705 readback word: bit15 pen-down (valid), [14:12] adcsel, [11:0] ADC. */
constexpr uint16_t kPenDown = 0x8000u;
constexpr uint16_t kAdcselX = 0x1000u;
constexpr uint16_t kAdcselY = 0x2000u;

/* Falcon CPLD = touch.dll DRIVER_GLOBALS. +0x00: OAL samples this on every
   OSMR1 pen-poll tick (ICIP bit27, PXA255 Table 4-36; nk.exe sub_800F33D4);
   if it != live pen state the poll reads "up" and fires spurious pen-ups
   mid-gesture. (+0x10 pen-up is the OAL's output from that poll - not CERF's.) */
constexpr uint32_t kCpldPenState  = 0xA3CC3000u;
constexpr uint32_t kCpldDataReady = 0xA3CC300Cu;
/* +0x18 bit0 re-arms the single-shot touch DMA (sub_18E5020(2)); the OAL's
   ENDINTR path sets bit2 not bit0, so CERF drives bit0 or only the first
   sample burst is delivered. */
constexpr uint32_t kCpldRearm     = 0xA3CC3018u;
constexpr uint32_t kCpldRearmBit  = 1u << 0;

/* WM9705 continuous mode streams at 93.75 Hz (CM_RATE_93, Linux wm97xx.h); pace
   delivery at the codec rate. One IRQ per host mouse-move on the UI thread
   instead floods the guest and blocks the Win32 message pump. */
constexpr DWORD kSamplePeriodMs = 11u;

class FalconTouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    ~FalconTouchInput() override {
        shutdown_.store(true, std::memory_order_release);
        if (wake_) SetEvent(wake_);
        if (pacer_.joinable()) pacer_.join();
        if (wake_) CloseHandle(wake_);
    }

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::FalconPC3xx;
    }

    void OnReady() override {
        wake_  = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        pacer_ = std::thread([this] { PacerMain(); });
    }

    /* UI thread: only store state, never touch the DMA/guest memory here. */
    void OnPenDown(int x, int y) override { SetPos(x, y); SetDown(true); }
    void OnPenMove(int x, int y) override { SetPos(x, y); }
    void OnPenUp(int, int) override       { SetDown(false); }
    void OnCaptureLost() override         { SetDown(false); }

private:
    void SetPos(int x, int y) {
        pos_x_.store(x, std::memory_order_relaxed);
        pos_y_.store(y, std::memory_order_relaxed);
    }
    void SetDown(bool down) {
        const bool was = pen_down_.exchange(down, std::memory_order_acq_rel);
        if (down != was) {
            LOG(Periph, "[FalconTouch] pen %s pos=(%d,%d)\n", down ? "DOWN" : "UP",
                pos_x_.load(std::memory_order_relaxed), pos_y_.load(std::memory_order_relaxed));
            if (!down) pen_up_pending_.store(true, std::memory_order_release);
            SetEvent(wake_);
        }
    }

    /* Dedicated thread (like Pxa255Ac97's audio thread): delivers WM9705 samples
       at the codec rate while the pen is down; parks otherwise. */
    void PacerMain() {
        auto& freeze = emu_.Get<EmulationFreeze>();
        while (!shutdown_.load(std::memory_order_acquire)) {
            if (pen_down_.load(std::memory_order_acquire)) {
                {
                    auto frozen = freeze.WorkerSection();
                    DeliverSample();
                }
                WaitForSingleObject(wake_, kSamplePeriodMs);   /* pace, wake on state change. */
            } else {
                if (pen_up_pending_.exchange(false, std::memory_order_acq_rel)) {
                    auto frozen = freeze.WorkerSection();
                    DeliverPenUp();
                }
                WaitForSingleObject(wake_, INFINITE);
            }
        }
    }

    void DeliverSample() {
        uint16_t rx, ry;
        RawFromScreen(pos_x_.load(std::memory_order_relaxed),
                      pos_y_.load(std::memory_order_relaxed), rx, ry);
        const uint16_t xw = static_cast<uint16_t>(kPenDown | kAdcselX | rx);
        const uint16_t yw = static_cast<uint16_t>(kPenDown | kAdcselY | ry);
        /* Both 16-byte ring buffers (4 decoder entries) hold the current sample:
           the IST median-filters across both, so an under-filled burst leaves
           stale entries and the moving coordinate is rejected as an outlier. */
        const uint16_t words[8] = { xw, yw, xw, yw, xw, yw, xw, yw };
        auto& disp = emu_.Get<PeripheralDispatcher>();
        disp.WriteWord(kCpldPenState, 1u);
        disp.WriteWord(kCpldDataReady, 1u);
        disp.WriteWord(kCpldRearm, disp.ReadWord(kCpldRearm) | kCpldRearmBit);
        emu_.Get<Pxa255Ac97>().PushTouchSample(words, 8u);
    }

    void DeliverPenUp() {
        /* Drop pen-state to 0; the OAL's next OSMR1 poll reads it, sets +0x10
           and raises sysintr 24 -> IST sub_18E2288 pen-up branch. */
        emu_.Get<PeripheralDispatcher>().WriteWord(kCpldPenState, 0u);
    }

    /* Inverse of the PC3xx factory cal (touch.dll TouchPanelCalibrateAPoint:
       out = 4*(M.raw+off)/DIV, in QUARTER-pixels; gwes.exe sub_20FA0 then /4's
       it to the screen pixel). Solve M.raw = sx*DIV - off - a /4 here (matching
       the cal's 4*) lands every tap at sx/4, i.e. clustered at the top-left. */
    static void RawFromScreen(int sx, int sy, uint16_t& raw_x, uint16_t& raw_y) {
        constexpr int64_t kM0 = 556, kM1 = -23408, kM2 = 85992351;
        constexpr int64_t kM3 = -30734, kM4 = 109, kM5 = 113344548;
        constexpr int64_t kDiv = 331292;
        constexpr int64_t kDet = kM0 * kM4 - kM1 * kM3;
        const int64_t bx = static_cast<int64_t>(sx) * kDiv - kM2;
        const int64_t by = static_cast<int64_t>(sy) * kDiv - kM5;
        raw_x = static_cast<uint16_t>(std::clamp<int64_t>((kM4 * bx - kM1 * by) / kDet, 0, 0x0FFF));
        raw_y = static_cast<uint16_t>(std::clamp<int64_t>((kM0 * by - kM3 * bx) / kDet, 0, 0x0FFF));
    }

    std::atomic<bool> pen_down_{false};
    std::atomic<bool> pen_up_pending_{false};
    std::atomic<bool> shutdown_{false};
    std::atomic<int>  pos_x_{0}, pos_y_{0};
    HANDLE            wake_ = nullptr;
    std::thread       pacer_;
};

}  // namespace

REGISTER_SERVICE_AS(FalconTouchInput, TouchInput);
