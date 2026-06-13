#define NOMINMAX

#include "../../host/keyboard_input.h"

#include <windows.h>

#include "../../core/cerf_emulator.h"
#include "../../host/keyboard_map.h"
#include "../../state/emulation_freeze.h"
#include "../board_detector.h"
#include "nec_mobilepro_900_pco_companion.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

namespace {

using Matrix = std::array<uint8_t, 13>;

/* Idle = no key pressed: active-low, so every bit set (keybddr sub_1BD3530). */
constexpr Matrix kIdleMatrix = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
                                0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};

/* keybddr emits on the first scan that sees a changed bit in pco's CURRENT
   snapshot (sub_1BD2D8C); one block per host edge loses a fast press (the release
   overwrites it before keybddr scans). The pacer re-streams + holds each state
   kMinStreams streams so keybddr scans each. Active-low: pressed = bit clear. */
class NecMobilePro900KeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    ~NecMobilePro900KeyboardInput() override {
        shutdown_.store(true, std::memory_order_release);
        if (wake_) SetEvent(wake_);
        if (pacer_.joinable()) pacer_.join();
        if (wake_) CloseHandle(wake_);
    }

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::NecMobilePro900;
    }

    void OnReady() override {
        wake_  = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        pacer_ = std::thread([this] { PacerMain(); });
    }

    void OnHostKey(uint8_t vk, bool key_up) override {
        uint32_t code;
        if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code)) return;
        const uint8_t bit  = static_cast<uint8_t>(code);
        const uint8_t byte = bit >> 3;
        const uint8_t mask = static_cast<uint8_t>(1u << (bit & 7u));
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (key_up) live_[byte] |= mask;                       /* set = released. */
            else        live_[byte] &= static_cast<uint8_t>(~mask); /* clear = pressed. */
            /* Queue the new state so the pacer holds it for kMinStreams before a
               later edge can overwrite it (the fast-press race). Collapse a
               no-op (a key the matrix already reflects). */
            if (pending_.empty() || pending_.back() != live_)
                pending_.push_back(live_);
        }
        SetEvent(wake_);
    }

private:
    /* Host stream cadence (matches the board's touch sampler). */
    static constexpr DWORD kSamplePeriodMs = 8u;
    /* Streams to hold each distinct state. Each stream wakes keybddr's scan; >=2
       guarantees a scan observes the state with margin for IST/scan jitter. */
    static constexpr int   kMinStreams = 2;

    void PacerMain() {
        auto& freeze = emu_.Get<EmulationFreeze>();
        Matrix cur      = kIdleMatrix;
        int    streamed = kMinStreams;   /* idle starts already settled. */
        while (!shutdown_.load(std::memory_order_acquire)) {
            bool active;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                /* Advance to the next queued state only once the current state
                   has been held long enough for keybddr to scan it. */
                if (streamed >= kMinStreams && !pending_.empty()) {
                    cur = pending_.front();
                    pending_.pop_front();
                    streamed = 0;
                }
            }
            {
                auto frozen = freeze.WorkerSection();
                emu_.Get<NecMobilePro900PcoCompanion>().SendKeyboardMatrix(cur.data());
            }
            ++streamed;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                /* Stay awake while a key is held (auto-repeat), while states are
                   queued, or until the current state has settled (kMinStreams). */
                active = !pending_.empty() || cur != kIdleMatrix ||
                         streamed < kMinStreams;
            }
            WaitForSingleObject(wake_, active ? kSamplePeriodMs : INFINITE);
        }
    }

    std::mutex         mtx_;
    Matrix             live_ = kIdleMatrix;   /* current live key state. */
    std::deque<Matrix> pending_;              /* states awaiting >=kMinStreams. */
    std::atomic<bool>  shutdown_{false};
    HANDLE             wake_  = nullptr;
    std::thread        pacer_;
};

}  /* namespace */

REGISTER_SERVICE_AS(NecMobilePro900KeyboardInput, KeyboardInput);
