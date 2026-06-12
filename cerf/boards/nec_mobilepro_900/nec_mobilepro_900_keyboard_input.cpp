#define NOMINMAX

#include "../../host/keyboard_input.h"

#include <windows.h>

#include "../../core/cerf_emulator.h"
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

/* Win32 VK -> NEC P530 key-matrix bit (0..103); 0xFF = unmapped. Bit index is the
   key's row in keybddr.dll's reader table dword_1BD1608, whose column-0 keycode is
   the VK for regular keys. Modifiers map both the generic (VK_SHIFT/CONTROL/MENU)
   and the L/R-specific VKs to the single matrix bit keybddr uses. */
constexpr std::array<uint8_t, 256> MakeVkToBit() {
    std::array<uint8_t, 256> m{};
    for (auto& e : m) e = 0xFFu;
    auto set = [&m](uint8_t vk, uint8_t bit) { m[vk] = bit; };

    /* Letters (VK 'A'..'Z'). */
    set(0x41, 64); set(0x42, 52); set(0x43, 74); set(0x44, 66); set(0x45, 58);
    set(0x46, 67); set(0x47, 44); set(0x48, 45); set(0x49, 39); set(0x4A, 46);
    set(0x4B, 47); set(0x4C, 88); set(0x4D, 54); set(0x4E, 53); set(0x4F, 80);
    set(0x50, 26); set(0x51, 56); set(0x52, 59); set(0x53, 65); set(0x54, 36);
    set(0x55, 38); set(0x56, 75); set(0x57, 57); set(0x58, 73); set(0x59, 37);
    set(0x5A, 72);
    /* Digits '0'..'9'. */
    set(0x30, 49); set(0x31, 40); set(0x32, 41); set(0x33, 42); set(0x34, 43);
    set(0x35, 60); set(0x36, 61); set(0x37, 62); set(0x38, 63); set(0x39, 48);
    /* Editing / control. */
    set(0x0D, 89); set(0x09, 25); set(0x08, 27); set(0x20, 95); set(0x1B, 32);
    set(0x2E, 33); set(0x14, 34);
    /* Arrows. */
    set(0x26, 83); set(0x28, 82); set(0x25, 91); set(0x27, 90);
    /* Punctuation (VK_OEM_*). */
    set(0xBA, 84); set(0xBB, 70); set(0xBC, 55); set(0xBD, 69); set(0xBE, 81);
    set(0xBF, 92); set(0xC0, 68); set(0xDB, 86); set(0xDC, 93); set(0xDD, 94);
    set(0xDE, 85);
    /* Modifiers (generic + L/R-specific) onto the matrix bits keybddr uses. */
    set(0x10, 4);  set(0xA0, 4);  set(0xA1, 4);   /* Shift. */
    set(0x11, 13); set(0xA2, 13); set(0xA3, 13);  /* Ctrl. */
    set(0x12, 22); set(0xA4, 22); set(0xA5, 31);  /* Alt (L=22, R=31). */
    return m;
}
constexpr std::array<uint8_t, 256> kVkToBit = MakeVkToBit();

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
        const uint8_t bit = kVkToBit[vk];
        if (bit == 0xFFu) return;
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
