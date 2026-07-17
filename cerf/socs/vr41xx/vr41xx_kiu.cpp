#include "vr41xx_kiu.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/emulation_freeze.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"
#include "vr41xx_icu.h"

#include <chrono>
#include <cstdint>

namespace {

constexpr uint32_t kBase = 0x0B000180u;   /* VR4102 UM Table 21-1, VR4121 UM Table 22-1. */

constexpr uint32_t kOffDat5    = 0x0Au;
constexpr uint32_t kOffScanRep = 0x10u;
constexpr uint32_t kOffScans   = 0x12u;
constexpr uint32_t kOffWks      = 0x14u;
constexpr uint32_t kOffWki      = 0x16u;
constexpr uint32_t kOffInt      = 0x18u;
constexpr uint32_t kOffRst      = 0x1Au;
constexpr uint32_t kOffGpen     = 0x1Cu;
constexpr uint32_t kOffScanLine = 0x1Eu;

constexpr uint16_t kKeyen       = 0x8000u;
constexpr uint16_t kScanRepMask = 0x83FFu;   /* VR4102 UM 21.2.2, VR4121 UM 22.2.2. */
constexpr uint16_t kScanRepPowerOn = 0x0001u;   /* D0 ATSCAN = 1 reset row (VR4102 UM 21.2.2, VR4121 UM 22.2.2). */
constexpr uint16_t kScanStart   = 1u << 2;   /* KIUSCANREP D2 SCANSTART (VR4102 UM 21.2.2, VR4121 UM 22.2.2). */
constexpr uint16_t kScanStp     = 1u << 3;   /* KIUSCANREP D3 SCANSTP (VR4102 UM 21.2.2, VR4121 UM 22.2.2). */
constexpr uint16_t kAtScan      = 1u << 0;   /* KIUSCANREP D0 ATSCAN auto-scan (VR4102 UM 21.2.2, VR4121 UM 22.2.2). */
constexpr uint16_t kKDatRdy     = 1u << 1;
constexpr uint16_t kIntMask     = 0x0007u;   /* KIUINT D2:0 (VR4102 UM 21.2.6, VR4121 UM 22.2.6). */
constexpr uint16_t kKiuRst      = 0x0001u;   /* KIURST D0 (VR4102 UM 21.2.7, VR4121 UM 22.2.7). */
constexpr uint32_t kWintvlUnitUs = 30u;   /* KIUWKI WINTVL unit (VR4102 UM 21.2.5, VR4121 UM 22.2.5). */
constexpr uint32_t kWorkerIdlePollUs = 16000u;

}  // namespace

REGISTER_SERVICE(Vr41xxKiu);

bool Vr41xxKiu::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    if (!bd) return false;
    const SocFamily soc = bd->GetSoc();
    return soc == SocFamily::VR4102 || soc == SocFamily::VR4121;
}

void Vr41xxKiu::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        std::lock_guard<std::mutex> lk(mtx_);
        ApplyResetLocked();
    });
    worker_ = std::thread([this] { WorkerLoop(); });
}

/* KIUSCANREP's reset rows are 0 but for D0 ATSCAN = 1 (VR4102 UM 21.2.2, VR4121 UM 22.2.2);
   KIUINT's are 0 (VR4102 UM 21.2.6, VR4121 UM 22.2.6). KIUDAT0-5 carry the key-matrix pin
   state, which no reset drives. */
void Vr41xxKiu::ApplyResetLocked() {
    scanrep_ = kScanRepPowerOn;
    causes_  = 0;
    scanning_ = false;
    wintvl_  = 0;   /* KIUWKI After-reset = 0 (VR4102 UM 21.2.5, VR4121 UM 22.2.5). */
    PublishCausesLocked();
}

bool Vr41xxKiu::EnabledLocked() const { return (scanrep_ & kKeyen) != 0; }

bool Vr41xxKiu::AnyKeyDownLocked() const {
    for (uint16_t w : matrix_)
        if (w) return true;
    return false;
}

void Vr41xxKiu::PublishCausesLocked() { emu_.Get<Vr41xxIcu>().SetKiuSource(causes_); }

uint16_t Vr41xxKiu::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - kBase;
    std::lock_guard<std::mutex> lk(mtx_);
    if (off <= kOffDat5 && (off & 1u) == 0u)
        return matrix_[off >> 1];
    switch (off) {
        case kOffScanRep: return scanrep_;
        /* SSTAT: 00 Stopped, 01 WaitKeyIn, 10 Interval Next Scan, 11 Scanning
           (VR4102 UM 21.2.3, VR4121 UM 22.2.3). */
        case kOffScans:
            if (!EnabledLocked()) return 0x0000u;
            return scanning_ ? 0x0002u : 0x0001u;
        case kOffInt:     return causes_;
        default:          HaltUnsupportedAccess("KIU ReadHalf", addr, 0);
    }
}

void Vr41xxKiu::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - kBase;
    std::lock_guard<std::mutex> lk(mtx_);
    switch (off) {
        /* KIUSCANREP D2 SCANSTART starts the scan sequencer (needs KEYEN, self-clears on
           start), D3 SCANSTP stops it (VR4102 UM 21.2.2, VR4121 UM 22.2.2); a running
           sequencer sets KIUINT D1 KDATRDY per completed scan (VR4102 UM 21.2.6, VR4121 UM 22.2.6). */
        case kOffScanRep:
            scanrep_ = value & kScanRepMask;
            if ((scanrep_ & kScanStart) && EnabledLocked()) scanning_ = true;
            /* ATSCAN auto-scan on key touch (VR4102 UM 21.2.2, VR4121 UM 22.2.2). */
            if ((scanrep_ & kAtScan) && EnabledLocked() && AnyKeyDownLocked())
                scanning_ = true;
            if ((scanrep_ & kScanStp) || !EnabledLocked())  scanning_ = false;
            if (scanning_) {
                causes_ |= kKDatRdy;
                PublishCausesLocked();
            }
            scanrep_ &= static_cast<uint16_t>(~kScanStart);
            NotifyWorker();
            return;
        case kOffInt:                                        /* W1C */
            causes_ &= static_cast<uint16_t>(~(value & kIntMask));
            /* KIUINT KDATRDY re-set per completed scan; KIUWKI WINTVL=0 = No Wait (VR4102 UM 21.2.3/21.2.5, VR4121 UM 22.2.3/22.2.5). */
            if (scanning_ && wintvl_ == 0) causes_ |= kKDatRdy;
            PublishCausesLocked();
            return;
        /* KIURST D0 "Cleared to 0 when 1 is written. 1: Reset" forcibly resets the KIU
           registers; D15:1 RFU "Write 0 when writing" (VR4102 UM 21.2.7, VR4121 UM 22.2.7). */
        case kOffRst:
            if (value & ~kKiuRst)
                HaltUnsupportedAccess("KIU KIURST reserved bits", addr, value);
            if (value & kKiuRst) ApplyResetLocked();
            return;
        /* KIUWKI WINTVL(9:0), inter-scan interval in 30us units (VR4102 UM 21.2.5, VR4121 UM 22.2.5). */
        case kOffWki:
            wintvl_ = value & 0x03FFu;
            NotifyWorker();
            return;
        /* KIUWKS / KIUGPEN / KIUSCANLINE (VR4102 UM 21.2.4/8/9, VR4121 UM 22.2.4/8/9). */
        case kOffWks: case kOffGpen: case kOffScanLine: return;
        default:          HaltUnsupportedAccess("KIU WriteHalf", addr, value);
    }
}

void Vr41xxKiu::SetKeyState(uint8_t matrix_index, bool pressed) {
    if (matrix_index >= 96u)
        HaltUnsupportedAccess("KIU SetKeyState index", matrix_index, pressed);

    auto frozen = emu_.Get<EmulationFreeze>().WorkerSection();
    std::lock_guard<std::mutex> lk(mtx_);

    const uint32_t reg  = matrix_index >> 4;
    const uint16_t mask = static_cast<uint16_t>(1u << (matrix_index & 15u));
    if (((matrix_[reg] & mask) != 0) == pressed)
        return;

    if (pressed) matrix_[reg] |= mask;
    else         matrix_[reg] &= static_cast<uint16_t>(~mask);

    if (!EnabledLocked())
        return;
    /* ATSCAN auto-scan starts automatically upon a key touch (VR4102 UM 21.2.2,
       VR4121 UM 22.2.2). */
    if (pressed && (scanrep_ & kAtScan)) scanning_ = true;
    causes_ |= kKDatRdy;
    PublishCausesLocked();
    NotifyWorker();
}

void Vr41xxKiu::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write(scanrep_);
    w.Write<uint8_t>(scanning_ ? 1u : 0u);
    w.Write(wintvl_);
}

void Vr41xxKiu::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    r.Read(scanrep_);
    uint8_t scanning = 0;
    r.Read(scanning);
    scanning_ = scanning != 0;
    r.Read(wintvl_);
    for (uint16_t& w : matrix_) w = 0;   /* No key is held after restore (hibernation.md). */
    causes_ = 0;
}

void Vr41xxKiu::PostRestore() {
    std::lock_guard<std::mutex> lk(mtx_);
    PublishCausesLocked();
    NotifyWorker();
}

void Vr41xxKiu::NotifyWorker() {
    std::lock_guard<std::mutex> g(cv_mtx_);
    cv_.notify_all();
}

void Vr41xxKiu::StopWorker() {
    stop_.store(true, std::memory_order_release);
    NotifyWorker();
    if (worker_.joinable()) worker_.join();
}

void Vr41xxKiu::WorkerLoop() {
    auto& freeze = emu_.Get<EmulationFreeze>();
    std::unique_lock<std::mutex> lk(cv_mtx_);
    while (!stop_.load(std::memory_order_acquire)) {
        lk.unlock();
        uint32_t wait_us = kWorkerIdlePollUs;
        {
            auto frozen = freeze.WorkerSection();
            std::lock_guard<std::mutex> sl(mtx_);
            /* KIUINT KDATRDY re-set per completed scan at the KIUWKI interval while the
               sequencer runs (VR4102 UM 21.2.3/21.2.5, VR4121 UM 22.2.3/22.2.5). */
            if (scanning_ && EnabledLocked() && wintvl_ != 0) {
                causes_ |= kKDatRdy;
                PublishCausesLocked();
                wait_us = static_cast<uint32_t>(wintvl_) * kWintvlUnitUs;
            }
        }
        lk.lock();
        if (stop_.load(std::memory_order_acquire)) break;
        cv_.wait_for(lk, std::chrono::microseconds(wait_us));
    }
}
