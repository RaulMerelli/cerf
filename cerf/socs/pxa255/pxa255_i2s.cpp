#include "pxa255_i2s.h"

REGISTER_SERVICE(Pxa255I2s);

uint32_t Pxa255I2s::ReadWord(uint32_t addr) {
    switch (addr - MmioBase()) {
    case kSACR0: return sacr0_;
    case kSACR1: return sacr1_;
    case kSASR0:
        /* Tx FIFO modeled as perpetually empty: TNF set, and TFS (DMA service
           request) set while enabled. If TNF/TFS ever read 0 here the guest audio
           driver's FIFO/DMA service loop blocks waiting to feed a FIFO that never
           drains — the classic audio-stub boot deadlock. */
        return kSasr0Tnf | ((sacr0_ & kSacr0Enb) ? kSasr0Tfs : 0u);
    case kSAIMR: return saimr_;
    case kSAICR: return 0u;          /* SAICR clears on write; nothing latched. */
    case kSADIV: return sadiv_;
    case kSADR:  return 0u;          /* Rx FIFO empty (no audio input). */
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Pxa255I2s::WriteWord(uint32_t addr, uint32_t value) {
    switch (addr - MmioBase()) {
    case kSACR0: sacr0_ = value; return;
    case kSACR1: sacr1_ = value; return;
    case kSAIMR: saimr_ = value; return;
    case kSAICR: return;             /* clear-on-write; no latched status to clear. */
    case kSADIV: sadiv_ = value; return;
    case kSADR:  return;             /* Tx sample consumed instantly (no output). */
    case kSASR0: return;             /* read-only status. */
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

void Pxa255I2s::SaveState(StateWriter& w) {
    w.Write(sacr0_); w.Write(sacr1_); w.Write(saimr_); w.Write(sadiv_);
}

void Pxa255I2s::RestoreState(StateReader& r) {
    r.Read(sacr0_); r.Read(sacr1_); r.Read(saimr_); r.Read(sadiv_);
}
