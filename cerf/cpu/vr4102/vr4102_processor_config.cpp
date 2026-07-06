#include "../mips_processor_config.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"

namespace {

/* NEC VR4102 (uPD30102), VR4100 CPU core. Values from the VR4102 User's Manual
   (per-accessor UM figure/table/chapter refs below). */
class Vr4102ProcessorConfig : public MipsProcessorConfig {
public:
    using MipsProcessorConfig::MipsProcessorConfig;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4102;
    }

    /* PRId (CP0 r15) Fig 5-16: bits[15:8] Imp = 0x0C for the VR4102, comp bits
       [31:16]=0 (legacy NEC), Rev[7:0]=0 (the UM says software must not depend on
       the revision field). */
    uint32_t     Prid()     const override { return 0x00000C00u; }

    /* On-chip TLB holds 32 entries (odd/even page pairs), fully associative
       (UM ch.5.1). */
    uint32_t     TlbSize()  const override { return 32u; }

    /* 32-bit fixed instruction encoding, MIPS III ISA, no MIPS16 (UM ch.3). */
    MipsIsaLevel IsaLevel() const override { return MipsIsaLevel::kMips3; }

    /* No FPU: COP1 raises Coprocessor Unusable (UM ch.1.5.6). */
    bool HasFpu()     const override { return false; }
    /* LL/SC raise reserved-instruction exception; LL bit eliminated (UM ch.3.1). */
    bool HasLlsc()    const override { return false; }
    /* CP0 Count (r9) + Compare (r11) present (UM Table 1-18). */
    bool HasCounter() const override { return true; }
    /* CP0 WatchLo (r18) + WatchHi (r19) present (UM Table 1-18). */
    bool HasWatch()   const override { return true; }
};

}  // namespace

REGISTER_SERVICE_AS(Vr4102ProcessorConfig, MipsProcessorConfig);
