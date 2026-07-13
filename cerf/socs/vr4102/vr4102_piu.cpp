#include "vr4102_piu.h"

#include "../guest_cpu_reset.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/emulation_freeze.h"
#include "../../state/state_stream.h"
#include "../vr41xx_icu.h"

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace {

/* PIU1 control-block offsets from 0x0B000120 (UM Table 19-1, p386). */
enum : uint32_t {
    kOffCnt  = 0x02, kOffInt  = 0x04, kOffSivl = 0x06, kOffStbl = 0x08,
    kOffCmd  = 0x0A,
};

/* PIUCNTREG fields (UM 19.3.1, p387-388). */
enum : uint16_t {
    kPadRst = 0x0001, kPiuPwr = 0x0002, kSeqEn = 0x0004,
    kScanStart = 0x0040, kScanStop = 0x0080, kAtStart = 0x0100, kAtStop = 0x0200,
    kCntStrobes = kPadRst | kScanStart | kScanStop,   /* forced/reset - unmodeled */
    kCntStored  = 0x033Eu,                            /* R/W config minus strobes */
};

/* PIUINTREG causes {D0,D2-D6} (W1C) + OVP D15 (UM 19.3.2, p390): D0 PENCHG,
   D3 PADPAGE0, D4 PADPAGE1, D5 PADADP, D6 PADCMD. */
enum : uint16_t {
    kPenChgIntr = 0x0001, kPage0Intr = 0x0008, kPage1Intr = 0x0010,
    kPadAdpIntr = 0x0020, kPadCmdIntr = 0x0040,
    kIntCauses = 0x007Du, kOvp = 0x8000u,
};

constexpr uint16_t kValid    = 0x8000u;  /* buffer D15 VALID (UM 19.3.9, p399) */
constexpr uint16_t kAdcMax   = 1023u;    /* PADDATA is 10-bit */
constexpr uint16_t kPressure = 0x03FFu;  /* Z: full contact (>= touch.dll floor 0x340) */

/* Command-scan A/D channel selects (PIUCMDREG.ADCMD[3:0], UM 19.3.5 p394): the
   touch driver reads ADIN0=X, ADIN1=Y, ADIN2=Z(pressure). */
enum : uint16_t { kAdcmdAdin0 = 4, kAdcmdAdin1 = 5, kAdcmdAdin2 = 6 };

/* touch.dll computes the raw coordinate ratiometrically as 379*ADINx/ADIN2
   (sub_15A0E24). Reporting ADIN2 (Z) = 379 makes ADIN0/ADIN1 equal pos_x/pos_y
   directly, i.e. the same [0,1023] raw extents coordinate mode produces (the
   applet's affine calibration then adapts to those extents). */
constexpr uint16_t kCmdZref = 379u;

/* Coordinate page-buffer offset (from 0x0B0002A0) -> {page, index in
   X+,X-,Y+,Y-,Z} (UM Table 19-4, p399). PIUAB0 (0x2B0, command-scan result) is
   served in ReadHalf2; PIUAB1-3 stay FATAL-first (unused). */
bool BufferSlot(uint32_t off, int* page, int* idx) {
    switch (off) {
        case 0x00: *page = 0; *idx = 0; return true;  /* PIUPB00 X+ */
        case 0x02: *page = 0; *idx = 1; return true;  /* PIUPB01 X- */
        case 0x04: *page = 0; *idx = 2; return true;  /* PIUPB02 Y+ */
        case 0x06: *page = 0; *idx = 3; return true;  /* PIUPB03 Y- */
        case 0x1C: *page = 0; *idx = 4; return true;  /* PIUPB04 Z  */
        case 0x08: *page = 1; *idx = 0; return true;  /* PIUPB10 X+ */
        case 0x0A: *page = 1; *idx = 1; return true;  /* PIUPB11 X- */
        case 0x0C: *page = 1; *idx = 2; return true;  /* PIUPB12 Y+ */
        case 0x0E: *page = 1; *idx = 3; return true;  /* PIUPB13 Y- */
        case 0x1E: *page = 1; *idx = 4; return true;  /* PIUPB14 Z  */
        default:   return false;
    }
}

}  /* namespace */

REGISTER_SERVICE(Vr4102Piu);

bool Vr4102Piu::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSoc() == SocFamily::VR4102;
}
void Vr4102Piu::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
    worker_ = std::thread([this] { WorkerLoop(); });
    /* The PIU is on the RSTOUT reset line; without a power-on reset on guest
       reboot the sequencer keeps its pre-reset PADSTATE and touch.dll's re-init
       handshake (sub_15A0568: spin until PIUCNTREG PADSTATE==0) never completes. */
    emu_.Get<GuestCpuReset>().RegisterResetListener([this] { ResetOnGuestReset(); });
}

/* ---- PIU1 control block (0x0B000120) ---- */

uint16_t Vr4102Piu::ReadHalf(uint32_t addr) {
    std::lock_guard<std::mutex> lk(mtx_);
    switch (addr - MmioBase()) {
        case kOffCnt:
            return static_cast<uint16_t>((pen_prev_ ? 0x4000u : 0u) |
                                         (pen_cur_  ? 0x2000u : 0u) |
                                         (static_cast<uint32_t>(state_) << 10) |
                                         (cnt_cfg_ & kCntStored));
        case kOffInt:  return intreg_;
        case kOffSivl: return sivl_;
        case kOffStbl: return stbl_;
        case kOffCmd:  return cmd_;
        default: HaltUnsupportedAccess("PIU ReadHalf", addr, 0);
    }
}

void Vr4102Piu::WriteHalf(uint32_t addr, uint16_t value) {
    std::lock_guard<std::mutex> lk(mtx_);
    switch (addr - MmioBase()) {
        case kOffCnt: ApplyCntWriteLocked(value); return;
        case kOffInt: {
            /* W1C the written cause bits; clearing a page cause invalidates that
               page's VALID bits (UM 19.3.9 p399). OVP is sequencer status. */
            const uint16_t clr = value & kIntCauses;
            intreg_ &= ~clr;
            if (clr & kPage0Intr) for (uint16_t& b : page_buf_[0]) b &= ~kValid;
            if (clr & kPage1Intr) for (uint16_t& b : page_buf_[1]) b &= ~kValid;
            DriveIcuLocked();
            return;
        }
        case kOffSivl: sivl_ = value & 0x07FFu; return;   /* SCANINTVAL (UM 19.3.3) */
        case kOffStbl: stbl_ = value & 0x003Fu; return;   /* STABLE (UM 19.3.4) */
        case kOffCmd:  cmd_  = value & 0x1FFFu; return;   /* PIUCMDREG (UM 19.3.5) */
        default: HaltUnsupportedAccess("PIU WriteHalf", addr, value);
    }
}

void Vr4102Piu::ApplyCntWriteLocked(uint16_t value) {
    /* PADRST / forced PADSCANSTART / PADSCANSTOP are not used by the CE 2.0 touch
       driver (touch.dll sub_15A0508/sub_15A04A8 use only PIUPWR/PIUSEQEN/PIUMODE/
       PADSCANTYPE/PADATSTART/PADATSTOP) - born-fatal until a path needs them. */
    if (value & kCntStrobes) {
        HaltUnsupportedAccess("PIU PIUCNTREG forced-scan/PADRST strobe", MmioBase() + kOffCnt, value);
    }
    const uint16_t old = cnt_cfg_;
    cnt_cfg_ = value & kCntStored;

    const bool pwr0 = (old & kPiuPwr) != 0, pwr1 = (cnt_cfg_ & kPiuPwr) != 0;
    if (!pwr0 && pwr1 && state_ == kStDisable) state_ = kStStandby;  /* Table 19-2 */
    else if (pwr0 && !pwr1)                    state_ = kStDisable;

    const bool seq0 = (old & kSeqEn) != 0, seq1 = (cnt_cfg_ & kSeqEn) != 0;
    if (!seq0 && seq1 && state_ == kStStandby) {
        /* Coordinate mode (PIUMODE=00) waits for a pen touch; command mode
           (PIUMODE=01) is CPU-driven and enters CmdScan directly - touch.dll
           TouchPanelBatteryGetInfo (sub_15A08C8) sets ADCMD/PIUMODE=01 then
           PIUSEQEN from Standby. */
        state_ = (PiuMode() == 1) ? kStCmdScan : kStWaitPenTouch;
    } else if (seq0 && !seq1 && state_ != kStDisable) {
        state_ = kStStandby;
    }

    /* Switching PIUMODE with the sequencer already running re-selects the scan
       type without a Standby round-trip: touch.dll sub_15A04A8 flips command
       back to coordinate at the end of a command burst while PIUSEQEN stays set. */
    if (((old >> 3) & 0x3u) != PiuMode() && seq1 &&
        state_ != kStDisable && state_ != kStStandby) {
        state_ = (PiuMode() == 1) ? kStCmdScan : kStWaitPenTouch;
    }

    /* A pen already down when the sequencer arms begins acquisition immediately:
       PenDataScan for coordinate mode, CmdScan for command mode. */
    if (pen_cur_ && (cnt_cfg_ & kSeqEn) && (cnt_cfg_ & kAtStart) &&
        state_ == kStWaitPenTouch) {
        state_ = kStPenDataScan;
        SampleOnceLocked();
        DriveIcuLocked();
        wake_.store(true, std::memory_order_release);
        cv_.notify_all();
    } else if (pen_cur_ && state_ == kStCmdScan) {
        CmdScanOnceLocked();
        DriveIcuLocked();
        wake_.store(true, std::memory_order_release);
        cv_.notify_all();
    }
}

/* ---- PIU2 data-buffer block (0x0B0002A0), via Vr4102Piu2Mmio ---- */

uint16_t Vr4102Piu::ReadHalf2(uint32_t off) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (off == 0x10) return adbuf_;   /* PIUAB0REG (0x2B0) command-scan result */
    int page = 0, idx = 0;
    if (BufferSlot(off, &page, &idx)) return page_buf_[page][idx];
    HaltUnsupportedAccess("PIU2 ReadHalf", 0x0B0002A0u + off, 0);
}
void Vr4102Piu::WriteHalf2(uint32_t off, uint16_t value) {
    /* The guest only reads the data buffers (touch.dll sub_15A0B4C); a write is
       unreached -> FATAL-first. */
    HaltUnsupportedAccess("PIU2 WriteHalf", 0x0B0002A0u + off, value);
}

/* ---- sampling + interrupt delivery ---- */

void Vr4102Piu::SampleOnceLocked() {
    /* A 4-wire resistive sample: X+/X- and Y+/Y- straddle the position so
       touch.dll (sub_15A0BB0) recovers rawX=(X+ - X- +1023)/2 = pos_x_,
       rawY=(Y+ - Y- +1023)/2 = pos_y_; Z is the contact-pressure reading. */
    const int page = next_page_;
    uint16_t (&buf)[5] = page_buf_[page];
    buf[0] = kValid | (pos_x_ & 0x3FFu);                    /* X+ */
    buf[1] = kValid | ((kAdcMax - pos_x_) & 0x3FFu);        /* X- */
    buf[2] = kValid | (pos_y_ & 0x3FFu);                    /* Y+ */
    buf[3] = kValid | ((kAdcMax - pos_y_) & 0x3FFu);        /* Y- */
    buf[4] = kValid | kPressure;                            /* Z  */

    intreg_ |= (page == 0) ? kPage0Intr : kPage1Intr;
    intreg_ = (intreg_ & ~kOvp) | ((page == 1) ? kOvp : 0u);  /* newest page = OVP */
    next_page_ ^= 1;
}

void Vr4102Piu::CmdScanOnceLocked() {
    /* Convert PIUCMDREG.ADCMD's channel into PIUAB0REG + raise PADCMDINTR (UM
       19.3.5/19.3.10; touch.dll sub_15A0E24 command branch). */
    uint16_t val;
    switch (cmd_ & 0x000Fu) {
        case kAdcmdAdin0: val = pos_x_;   break;   /* X */
        case kAdcmdAdin1: val = pos_y_;   break;   /* Y */
        case kAdcmdAdin2: val = kCmdZref; break;   /* Z (pressure) */
        default: HaltUnsupportedAccess("PIU command-scan ADCMD",
                                       MmioBase() + kOffCmd, cmd_ & 0x000Fu);
    }
    adbuf_ = kValid | (val & 0x3FFu);
    intreg_ |= kPadCmdIntr;
}

void Vr4102Piu::DriveIcuLocked() {
    /* ICU PIUINT (0x0B000082) mirrors PIUINTREG's cause bits {D0,D2-D6}; the ICU
       raises SYSINT1 PIUINTR when (PIUINT & MPIU) (guest ICU decode sub_9F002050). */
    emu_.Get<Vr41xxIcu>().SetPiuSource(intreg_ & kIntCauses);
}

uint32_t Vr4102Piu::IntervalMsLocked() const {
    const uint32_t us = static_cast<uint32_t>(sivl_ & 0x07FFu) * 30u;  /* UM 19.3.3 */
    return std::clamp<uint32_t>(us / 1000u, 5u, 100u);
}

/* ---- host stylus source ---- */

void Vr4102Piu::SetPen(bool down, uint16_t pos_x, uint16_t pos_y) {
    {
        /* Host input thread mutates guest-visible PIU state and drives the ICU
           interrupt - take the freeze barrier (before mtx_) so a hibernation
           snapshot cannot tear across the PIU and the ICU it drives. */
        auto frozen = emu_.Get<EmulationFreeze>().WorkerSection();
        std::lock_guard<std::mutex> lk(mtx_);
        pos_x_ = pos_x & 0x3FFu;
        pos_y_ = pos_y & 0x3FFu;
        if (down != pen_cur_) {
            pen_prev_ = pen_cur_;
            pen_cur_  = down;
            intreg_ |= kPenChgIntr;
            if (down) {
                if ((cnt_cfg_ & kSeqEn) && (cnt_cfg_ & kAtStart) &&
                    state_ == kStWaitPenTouch) {
                    state_ = kStPenDataScan;
                }
                if (state_ == kStPenDataScan) SampleOnceLocked();
            } else if (state_ == kStPenDataScan && (cnt_cfg_ & kAtStop)) {
                state_ = kStWaitPenTouch;   /* release -> back to wait (UM Table 19-2) */
            }
            DriveIcuLocked();
        }
    }
    wake_.store(true, std::memory_order_release);
    cv_.notify_all();
}

/* ---- worker: re-sample while the pen is held, at the SCANINTVAL cadence ---- */

void Vr4102Piu::StopWorker() {
    stop_.store(true, std::memory_order_release);
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void Vr4102Piu::WorkerLoop() {
    auto& freeze = emu_.Get<EmulationFreeze>();
    std::unique_lock<std::mutex> lk(cv_mtx_);
    while (!stop_.load(std::memory_order_acquire)) {
        lk.unlock();
        bool sampled = false;
        uint32_t interval = 20;
        {
            auto frozen = freeze.WorkerSection();
            std::lock_guard<std::mutex> sl(mtx_);
            if (pen_cur_ && state_ == kStPenDataScan) {
                SampleOnceLocked();
                DriveIcuLocked();
                interval = IntervalMsLocked();
                sampled = true;
            } else if (pen_cur_ && state_ == kStCmdScan) {
                CmdScanOnceLocked();
                DriveIcuLocked();
                interval = IntervalMsLocked();
                sampled = true;
            }
        }
        lk.lock();
        if (stop_.load(std::memory_order_acquire)) break;
        if (sampled) {
            cv_.wait_for(lk, std::chrono::milliseconds(interval));
        } else {
            cv_.wait(lk, [this] {
                return stop_.load(std::memory_order_acquire) ||
                       wake_.exchange(false, std::memory_order_acq_rel);
            });
        }
    }
}

/* ---- guest reset (RSTOUT) ---- */

void Vr4102Piu::ResetOnGuestReset() {
    /* JIT thread at reset delivery: return every register + pen/buffer state to
       power-on (the member initializers), so touch.dll's re-init handshake sees
       PADSTATE=kStDisable. intreg_=0 also clears the PIU source in the ICU. */
    std::lock_guard<std::mutex> lk(mtx_);
    state_    = kStDisable;
    cnt_cfg_  = 0;
    intreg_   = 0;
    sivl_     = 0x0007u;
    stbl_     = 0x0007u;
    cmd_      = 0x000Fu;
    pen_cur_  = pen_prev_ = false;
    pos_x_    = pos_y_ = 0;
    for (auto& pg : page_buf_) for (uint16_t& b : pg) b = 0;
    next_page_ = 0;
    adbuf_     = 0;
    DriveIcuLocked();
}

/* ---- hibernation ---- */

void Vr4102Piu::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write(state_); w.Write(cnt_cfg_); w.Write(intreg_);
    w.Write(sivl_); w.Write(stbl_); w.Write(cmd_);
    w.Write<uint8_t>(pen_cur_ ? 1 : 0); w.Write<uint8_t>(pen_prev_ ? 1 : 0);
    w.Write(pos_x_); w.Write(pos_y_);
    for (auto& pg : page_buf_) for (uint16_t b : pg) w.Write(b);
    w.Write(next_page_);
    w.Write(adbuf_);
}
void Vr4102Piu::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    r.Read(state_); r.Read(cnt_cfg_); r.Read(intreg_);
    r.Read(sivl_); r.Read(stbl_); r.Read(cmd_);
    uint8_t cur = 0, prev = 0; r.Read(cur); r.Read(prev);
    r.Read(pos_x_); r.Read(pos_y_);
    for (auto& pg : page_buf_) for (uint16_t& b : pg) r.Read(b);
    r.Read(next_page_);
    r.Read(adbuf_);
    /* No host pen exists after restore: drop an in-flight contact so the worker
       does not synthesize phantom samples at a stale position. */
    pen_cur_ = pen_prev_ = false;
    if (state_ == kStPenDataScan || state_ == kStIntervalNext) state_ = kStWaitPenTouch;
}
void Vr4102Piu::PostRestore() {
    std::lock_guard<std::mutex> lk(mtx_);
    DriveIcuLocked();   /* re-assert the PIU interrupt indication into the ICU */
}
