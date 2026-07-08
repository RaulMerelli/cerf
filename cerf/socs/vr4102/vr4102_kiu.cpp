#include "vr4102_kiu.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/emulation_freeze.h"
#include "../../state/state_stream.h"
#include "vr4102_icu.h"

#include <cstdint>

namespace {

constexpr uint32_t kBase = 0x0B000180u;   /* UM Table 21-1, p423. */

constexpr uint32_t kOffDat5    = 0x0Au;
constexpr uint32_t kOffScanRep = 0x10u;
constexpr uint32_t kOffScans   = 0x12u;
constexpr uint32_t kOffWks      = 0x14u;
constexpr uint32_t kOffWki      = 0x16u;
constexpr uint32_t kOffInt      = 0x18u;
constexpr uint32_t kOffGpen     = 0x1Cu;
constexpr uint32_t kOffScanLine = 0x1Eu;

constexpr uint16_t kKeyen       = 0x8000u;
constexpr uint16_t kScanRepMask = 0x83FFu;   /* UM 21.2.2, p425. */
constexpr uint16_t kKDatRdy     = 1u << 1;
constexpr uint16_t kIntMask     = 0x0007u;

}  // namespace

REGISTER_SERVICE(Vr4102Kiu);

bool Vr4102Kiu::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSoc() == SocFamily::VR4102;
}

void Vr4102Kiu::OnReady() { emu_.Get<PeripheralDispatcher>().Register(this); }

bool Vr4102Kiu::EnabledLocked() const { return (scanrep_ & kKeyen) != 0; }

void Vr4102Kiu::PublishCausesLocked() { emu_.Get<Vr4102Icu>().SetKiuSource(causes_); }

uint16_t Vr4102Kiu::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - kBase;
    std::lock_guard<std::mutex> lk(mtx_);
    if (off <= kOffDat5 && (off & 1u) == 0u)
        return matrix_[off >> 1];
    switch (off) {
        case kOffScanRep: return scanrep_;
        case kOffScans:   return EnabledLocked() ? 0x0001u : 0x0000u;   /* SSTAT WaitKeyIn/Stopped, UM 21.2.3 */
        case kOffInt:     return causes_;
        default:          HaltUnsupportedAccess("KIU ReadHalf", addr, 0);
    }
}

void Vr4102Kiu::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - kBase;
    std::lock_guard<std::mutex> lk(mtx_);
    switch (off) {
        case kOffScanRep: scanrep_ = value & kScanRepMask; return;
        case kOffInt:                                        /* W1C */
            causes_ &= static_cast<uint16_t>(~(value & kIntMask));
            PublishCausesLocked();
            return;
        /* Scan timing / GP-output config with no CERF effect: accept the write, read
           stays born-FATAL (UM 21.2.4/5/8/9). */
        case kOffWks: case kOffWki: case kOffGpen: case kOffScanLine: return;
        default:          HaltUnsupportedAccess("KIU WriteHalf", addr, value);
    }
}

void Vr4102Kiu::SetKeyState(uint8_t matrix_index, bool pressed) {
    if (matrix_index >= 96u)
        HaltUnsupportedAccess("KIU SetKeyState index", matrix_index, pressed);

    /* Freeze against a hibernation snapshot (hibernation.md). */
    auto frozen = emu_.Get<EmulationFreeze>().WorkerSection();
    std::lock_guard<std::mutex> lk(mtx_);

    const uint32_t reg  = matrix_index >> 4;
    const uint16_t mask = static_cast<uint16_t>(1u << (matrix_index & 15u));
    if (((matrix_[reg] & mask) != 0) == pressed) return;

    if (pressed) matrix_[reg] |= mask;
    else         matrix_[reg] &= static_cast<uint16_t>(~mask);

    if (!EnabledLocked()) return;
    causes_ |= kKDatRdy;
    PublishCausesLocked();
}

void Vr4102Kiu::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write(scanrep_);
}

void Vr4102Kiu::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    r.Read(scanrep_);
    /* No key is held after restore (hibernation.md). */
    for (uint16_t& w : matrix_) w = 0;
    causes_ = 0;
}

void Vr4102Kiu::PostRestore() {
    std::lock_guard<std::mutex> lk(mtx_);
    PublishCausesLocked();
}
