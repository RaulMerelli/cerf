#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_detector.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>
#include <cstdio>
#include <mutex>

namespace {

/* INT720.C:55 RMWs LED_DISCRETE every 1ms — reads MUST return
   last-written value. LED_ALPHA mirrors 64KB per P2DEBUG.H:135;
   DEBUG.C:184 trips it via wIndex*4 & 0xFFFF. */

constexpr uint32_t kHkeepFpgaPaBase   = 0x04040000u;
constexpr uint32_t kHkeepFpgaSize     = 0x00030000u;

constexpr uint32_t kLedDiscretePa     = 0x04040000u;  /* P2DEBUG.H line 133 */
constexpr uint32_t kLedAlphaPa        = 0x04060000u;  /* P2DEBUG.H line 134 */
constexpr uint32_t kLedAlphaMirror    = 0x0000FFFFu;  /* P2DEBUG.H line 135 */

class OdoArm720HkeepFpga : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardDetector>().GetBoard() == Board::OdoArm720;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kHkeepFpgaPaBase; }
    uint32_t MmioSize() const override { return kHkeepFpgaSize; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

private:
    /* Returns true and *out_is_alpha if addr resolves to a known
       LED register. False otherwise. */
    bool ClassifyAddr(uint32_t addr, bool* out_is_alpha) const;

    /* Decode + log alpha-LED write as 4-char ASCII (the kernel
       writes 7-segment-style ASCII codes; CFWP2.C OEMInit's
       0xECEEECEE prints as ".ECEE" backwards = "EECE." or similar
       7-segment pattern depending on hardware decoding). */
    void LogAlphaWrite(uint32_t addr, uint32_t value);

    mutable std::mutex state_mutex_;
    uint32_t           led_discrete_value_ = 0;
    uint32_t           led_alpha_value_    = 0;
};

bool OdoArm720HkeepFpga::ClassifyAddr(uint32_t addr, bool* out_is_alpha) const {
    if (addr == kLedDiscretePa) {
        *out_is_alpha = false;
        return true;
    }
    /* Alpha mirror: LED_ALPHA + (offset & LED_ALPHA_MIRROR).
       Any DWORD-aligned address in [LED_ALPHA, LED_ALPHA + 0xFFFF]
       hits the single alpha register. */
    if (addr >= kLedAlphaPa && addr <= kLedAlphaPa + kLedAlphaMirror) {
        *out_is_alpha = true;
        return true;
    }
    return false;
}

uint32_t OdoArm720HkeepFpga::ReadWord(uint32_t addr) {
    bool is_alpha = false;
    if (!ClassifyAddr(addr, &is_alpha)) {
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    uint32_t value;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        value = is_alpha ? led_alpha_value_ : led_discrete_value_;
    }

#if CERF_DEV_MODE
    /* LED_DISCRETE reads can fire at ~1 kHz when the timer ISR
       is running (INT720.C line 55 read-modify-write). Per
       agent_docs/rules.md § "Simple LOG verbose lines",
       high-frequency LOG sites must not ship in production. */
    LOG(Board, "Odo HKEEP %s read  [0x%08X] -> 0x%08X\n",
        is_alpha ? "LED_ALPHA" : "LED_DISCRETE", addr, value);
#endif
    return value;
}

void OdoArm720HkeepFpga::WriteWord(uint32_t addr, uint32_t value) {
    bool is_alpha = false;
    if (!ClassifyAddr(addr, &is_alpha)) {
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        if (is_alpha) led_alpha_value_    = value;
        else          led_discrete_value_ = value;
    }

    if (is_alpha) {
        LogAlphaWrite(addr, value);
    } else {
#if CERF_DEV_MODE
        /* Same 1 kHz consideration as the read path. */
        LOG(Board, "Odo HKEEP LED_DISCRETE = 0x%08X\n", value);
#endif
    }
}

void OdoArm720HkeepFpga::LogAlphaWrite(uint32_t addr, uint32_t value) {
    /* LED_ALPHA writes are LOW-frequency on this build path
       (boot stub WhereAmI, OEMInit's 0xECEEECEE, and the very
       rare OEMWriteDebugLED-triggered prints). Permanent LOG is
       acceptable — these are major boot landmarks. */
    char as_ascii[5] = {
        static_cast<char>((value      ) & 0xFFu),
        static_cast<char>((value >>  8) & 0xFFu),
        static_cast<char>((value >> 16) & 0xFFu),
        static_cast<char>((value >> 24) & 0xFFu),
        0,
    };
    auto sanitize = [](char c) -> char {
        return (c >= 0x20 && c < 0x7F) ? c : '.';
    };
    /* DEBUG.C line 184 derives the index from
       `(wIndex * sizeof(ULONG)) & LED_ALPHA_MIRROR`. Showing the
       byte offset so a future reader can recover wIndex (= byte
       offset / 4) for diagnostic correlation. */
    const uint32_t offset = addr - kLedAlphaPa;
    LOG(Board, "Odo HKEEP LED_ALPHA +0x%04X = 0x%08X (\"%c%c%c%c\")\n",
        offset, value,
        sanitize(as_ascii[0]),
        sanitize(as_ascii[1]),
        sanitize(as_ascii[2]),
        sanitize(as_ascii[3]));
}

}  /* namespace */

REGISTER_SERVICE(OdoArm720HkeepFpga);
