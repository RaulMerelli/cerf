#include "vrc5477_intc.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {
enum : uint32_t {
    kINTCTRL0 = 0x00, kINTCTRL1 = 0x04, kINTCTRL2 = 0x08, kINTCTRL3 = 0x0C,
    kINT0STAT = 0x20, kINT1STAT = 0x28, kINT2STAT = 0x30, kINT3STAT = 0x38,
    kINT4STAT = 0x40, kNMISTAT  = 0x50, kINTCLR32 = 0x68,
    kINTPPES0 = 0x70, kINTPPES1 = 0x78, kCPUSTAT  = 0x80, kBUSCTRL  = 0x88,
};
}  /* namespace */

REGISTER_SERVICE(Vrc5477Intc);

bool Vrc5477Intc::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetSoc() == SocFamily::VR5500;
}

uint32_t Vrc5477Intc::StatusForLine(uint32_t n) const {
    uint32_t s = 0;
    for (uint32_t irq = 0; irq < 32; ++irq) {
        const uint32_t nib = (intctrl_[irq >> 3] >> ((irq & 7u) * 4u)) & 0xFu;
        if ((nib & 0x8u) && (nib & 0x7u) == n && (pending_ & (1u << irq))) {
            s |= (1u << irq);
        }
    }
    return s;
}

uint32_t Vrc5477Intc::ReadReg(uint32_t off) {
    switch (off) {
        case kINTCTRL0: return intctrl_[0];
        case kINTCTRL1: return intctrl_[1];
        case kINTCTRL2: return intctrl_[2];
        case kINTCTRL3: return intctrl_[3];
        case kINT0STAT: return StatusForLine(0);
        case kINT1STAT: return StatusForLine(1);
        case kINT2STAT: return StatusForLine(2);
        case kINT3STAT: return StatusForLine(3);
        case kINT4STAT: return StatusForLine(4);
        case kNMISTAT:  return nmistat_;
        case kINTCLR32: return pending_;
        case kINTPPES0: return intppes0_;
        case kINTPPES1: return intppes1_;
        case kCPUSTAT:  return cpustat_;
        case kBUSCTRL:  return busctrl_;
        default:        return 0;
    }
}

void Vrc5477Intc::WriteReg(uint32_t off, uint32_t value) {
    switch (off) {
        case kINTCTRL0: intctrl_[0] = value; break;
        case kINTCTRL1: intctrl_[1] = value; break;
        case kINTCTRL2: intctrl_[2] = value; break;
        case kINTCTRL3: intctrl_[3] = value; break;
        case kINTCLR32: pending_ &= ~value; break;   /* write-1-to-clear latched edges */
        case kINTPPES0: intppes0_ = value; break;
        case kINTPPES1: intppes1_ = value; break;
        case kCPUSTAT:  cpustat_  = value; break;
        case kBUSCTRL:  busctrl_  = value; break;
        default:        break;                        /* INTnSTAT/NMISTAT read-only */
    }
}

void Vrc5477Intc::SaveState(StateWriter& w) {
    for (uint32_t v : intctrl_) w.Write(v);
    w.Write(intppes0_); w.Write(intppes1_);
    w.Write(cpustat_);  w.Write(busctrl_); w.Write(nmistat_); w.Write(pending_);
}

void Vrc5477Intc::RestoreState(StateReader& r) {
    for (uint32_t& v : intctrl_) r.Read(v);
    r.Read(intppes0_); r.Read(intppes1_);
    r.Read(cpustat_);  r.Read(busctrl_); r.Read(nmistat_); r.Read(pending_);
}
