#pragma once

#include <cstdint>

#include "../../core/service.h"

class MipsJit;
struct MipsCpuState;

/* Per-core CP0 exception delivery. */
class MipsExceptionModel : public Service {
public:
    using Service::Service;

    /* Set EPC/Cause/Status for a synchronous exception and vector the guest PC.
       `refill_eligible` is true only for a TLB miss with no matching entry. */
    virtual void Enter(MipsJit* jit, uint32_t cause, bool refill_eligible) = 0;

    /* BadVAddr / Context / EntryHi writeback after a TLB fault at `va`. */
    virtual void SetMmuFaultRegs(MipsJit* jit, uint32_t va) = 0;

    /* Whether Status permits a hardware interrupt right now. The cores nest
       exceptions differently, so the bits that suppress delivery differ. */
    virtual bool InterruptsEnabled(const MipsCpuState& s) const = 0;
};
