#include "s3c2410_io_port.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../irq_controller.h"

namespace {

/* S3C2410 chip-ID per user manual section 9.6 (GSTATUS1). The
   DeviceEmulator BSP and other S3C2410-targeting OALs probe this
   register to confirm chip family before writing GPIO config. */
constexpr uint32_t kRegOffsetGstatus1   = 0xB0u;
constexpr uint32_t kGstatus1ChipIdValue = 0x32410000u;

constexpr uint32_t kSlotExtint0 = 0x88u / 4u;
constexpr uint32_t kSlotExtint1 = 0x8Cu / 4u;
constexpr uint32_t kSlotExtint2 = 0x90u / 4u;

/* EXTINT trigger types (DeviceEmulator IOGPIO::RaiseInterrupt). */
constexpr int kTrigLowLevel  = 0;
constexpr int kTrigHighLevel = 1;

bool IsFallingTrig(int t) { return t == 2 || t == 3; }
bool IsBothEdgeTrig(int t) { return t == 6 || t == 7; }

/* EINT4..7 share SRCPND bit 4, EINT8..23 share bit 5 (silicon
   rollup); OAL demuxes via EINTPEND. EINT0..3 are direct SRCPND
   sources and never appear in EINTPEND / EINTMASK. */
int MainSourceBitForEint(int n) {
    if (n <= 3) return n;
    if (n <= 7) return 4;
    return 5;
}

uint32_t RollupBitsFor(uint32_t eint_bits) {
    uint32_t out = 0u;
    if (eint_bits & 0x000000F0u) out |= 1u << 4;
    if (eint_bits & 0x00FFFF00u) out |= 1u << 5;
    return out;
}

}  /* namespace */

bool S3C2410IoPort::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetSoc() == SocFamily::S3C2410;
}

void S3C2410IoPort::OnReady() {
    storage_[kRegOffsetGstatus1 / 4u] = kGstatus1ChipIdValue;
    emu_.Get<PeripheralDispatcher>().Register(this);
}

int S3C2410IoPort::ExtintTypeLocked(int n) const {
    if (n < 8)  return (storage_[kSlotExtint0] >> (n * 4)) & 0x7;
    if (n < 16) return (storage_[kSlotExtint1] >> ((n - 8) * 4)) & 0x7;
    return (storage_[kSlotExtint2] >> ((n - 16) * 4)) & 0x7;
}

uint32_t S3C2410IoPort::ReevaluateLocked() {
    /* Still-asserted level lines re-latch EINTPEND when the guest
       clears it — without the re-latch, a line asserting inside the
       guest's masked IST window is lost and the driver stalls until
       unrelated traffic re-edges the line. */
    storage_[kSlotEintPend] |= eint_level_ & ~storage_[kSlotEintMask];
    return RollupBitsFor(storage_[kSlotEintPend] &
                         ~storage_[kSlotEintMask]);
}

uint32_t S3C2410IoPort::ReadWord(uint32_t addr) {
    const uint32_t slot = (addr - MmioBase()) / 4u;
    if (slot >= kSlotCount) {
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }
    uint32_t value;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        value = storage_[slot];
    }
    return value;
}

void S3C2410IoPort::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t slot = (addr - MmioBase()) / 4u;
    if (slot >= kSlotCount) {
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
    uint32_t raise = 0u;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        switch (slot) {
            case kSlotEintPend:
                /* W1C — the kernel ISR drops the pend bit, then any
                   still-high level line immediately re-latches it. */
                storage_[slot] &= ~value;
                raise = ReevaluateLocked();
                break;
            case kSlotEintMask:
                storage_[slot] = value;
                /* Unmasking with a pend (or held level line) must fire
                   the rollup now — the combinational EINTPEND/EINTMASK
                   AND drives SRCPND on silicon (DeviceEmulator raises
                   on EINTMASK writes the same way). */
                raise = ReevaluateLocked();
                break;
            default:
                storage_[slot] = value;
                break;
        }
    }
    if (raise) {
        auto& irq = emu_.Get<IrqController>();
        for (int bit = 4; bit <= 5; ++bit) {
            if (raise & (1u << bit)) irq.AssertIrq(bit);
        }
    }
}

void S3C2410IoPort::AssertEint(int n) {
    if (n < 0 || n >= 24) {
        LOG(Caution, "S3C2410IoPort::AssertEint: n=%d out of range "
                "(EINT0..23)\n", n);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (n <= 3) {
        /* Direct SRCPND sources, no EINTPEND/EINTMASK stage. */
        emu_.Get<IrqController>().AssertIrq(MainSourceBitForEint(n));
        return;
    }

    bool report = false;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        const uint32_t bit = 1u << n;
        const int trig = ExtintTypeLocked(n);
        if (trig == kTrigHighLevel) {
            eint_level_ |= bit;
        }
        if (trig == kTrigLowLevel || IsFallingTrig(trig)) {
            /* Low-level: line going high is the inactive state.
               Falling-edge: the rising transition is not the trigger
               edge. Neither latches a pend here. */
        } else {
            storage_[kSlotEintPend] |= bit;
            report = (storage_[kSlotEintMask] & bit) == 0;
        }
    }
    if (report) {
        emu_.Get<IrqController>().AssertIrq(MainSourceBitForEint(n));
    }
}

void S3C2410IoPort::ClearEint(int n) {
    if (n < 0 || n >= 24) {
        LOG(Caution, "S3C2410IoPort::ClearEint: n=%d out of range "
                "(EINT0..23)\n", n);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (n <= 3) return;     /* direct SRCPND source — no pend stage */

    bool report = false;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        const uint32_t bit = 1u << n;
        const int trig = ExtintTypeLocked(n);
        if (trig == kTrigHighLevel) {
            eint_level_ &= ~bit;
        } else if (IsFallingTrig(trig) || IsBothEdgeTrig(trig)) {
            /* The line dropping IS the trigger edge for these types. */
            storage_[kSlotEintPend] |= bit;
            report = (storage_[kSlotEintMask] & bit) == 0;
        }
    }
    if (report) {
        emu_.Get<IrqController>().AssertIrq(MainSourceBitForEint(n));
    }
}

void S3C2410IoPort::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    w.WriteBytes(storage_, sizeof(storage_));
    w.Write(eint_level_);
}

void S3C2410IoPort::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    r.ReadBytes(storage_, sizeof(storage_));
    r.Read(eint_level_);
}

REGISTER_SERVICE(S3C2410IoPort);
