#define NOMINMAX

#include "../../host/keyboard_input.h"

#include <windows.h>

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../host/keyboard_map.h"
#include "../../host/keyboard_router.h"
#include "../../state/emulation_freeze.h"
#include "../board_context.h"
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

/* The PICO keyboard is request/reply (Linux pic-pxa2xx.c): a key-down sends one
   0x12 that wakes keybddr, which then polls the matrix via on-demand 0x13 requests.
   The pacer caches each host state for that 0x13 reply and sends one 0x12 on a
   key-down edge. Active-low: pressed = bit clear. */
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
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::NecMobilePro900;
    }

    void OnReady() override {
        wake_  = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        pacer_ = std::thread([this] { PacerMain(); });
        emu_.Get<KeyboardRouter>().Register(this);
    }

    void OnHostKey(uint8_t vk, bool key_up) override {
        /* T0 of the keystroke->blit latency bisection: the host key event, in the
           same QPC t+ timeline as the guest [PCO-KBD]/[PCO-VK]/[INPUTDISP] probes. */
        LOG(Board, "[NEC-KEY] vk=0x%02X up=%d\n", vk, key_up ? 1 : 0);
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
    /* Pacer tick: re-checks the queue and ages each state's dwell. */
    static constexpr DWORD kSamplePeriodMs = 8u;
    /* Ticks to hold each distinct state in the cached matrix so keybddr's ~5 ms
       poll observes it; >=2 gives margin for IST/scan jitter. */
    static constexpr int   kMinStreams = 2;

    void PacerMain() {
        auto& freeze = emu_.Get<EmulationFreeze>();
        auto& pco    = emu_.Get<NecMobilePro900PcoCompanion>();
        Matrix cur      = kIdleMatrix;
        Matrix prev     = kIdleMatrix;
        int    streamed = kMinStreams;   /* idle starts already settled. */
        while (!shutdown_.load(std::memory_order_acquire)) {
            bool advanced = false;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                /* Advance to the next queued state only once the current state
                   has been held long enough for keybddr to poll it. */
                if (streamed >= kMinStreams && !pending_.empty()) {
                    cur = pending_.front();
                    pending_.pop_front();
                    streamed = 0;
                    advanced = true;
                }
            }
            if (advanced) {
                auto frozen = freeze.WorkerSection();
                pco.SetKeyMatrix(cur.data());                       /* answer on-demand 0x13 */
                if (HasNewKeyDown(prev, cur)) pco.NotifyKeyDown();  /* 0x12 wakes idle keybddr */
                prev = cur;
            }
            ++streamed;
            bool active;
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

    /* Active-low: a newly-pressed key is a bit clear in cur but set in prev. */
    static bool HasNewKeyDown(const Matrix& prev, const Matrix& cur) {
        for (size_t i = 0; i < cur.size(); ++i)
            if (static_cast<uint8_t>(~cur[i] & prev[i]) != 0u) return true;
        return false;
    }

    std::mutex         mtx_;
    Matrix             live_ = kIdleMatrix;   /* current live key state. */
    std::deque<Matrix> pending_;              /* states awaiting >=kMinStreams. */
    std::atomic<bool>  shutdown_{false};
    HANDLE             wake_  = nullptr;
    std::thread        pacer_;
};

}  /* namespace */

REGISTER_SERVICE(NecMobilePro900KeyboardInput);
