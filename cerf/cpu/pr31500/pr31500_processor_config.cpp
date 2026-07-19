#include "../mips_processor_config.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"

namespace {

/* Philips PR31500 "Poseidon", MIPS R3000 core (PR31500.PDF p.2). The TLB and CP0
   registers are the Toshiba TX39 family's: TMPR3911.pdf ch.3, TMPR39xx-um ch.2. */
class Pr31500ProcessorConfig : public MipsProcessorConfig {
public:
    using MipsProcessorConfig::MipsProcessorConfig;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::PR31500;
    }

    /* PRId (CP0 r15): Imp[15:8] = 0x22, "R3900 Processor Core ID", reset value
       0x22 (TMPR39xx-um Fig 6-9). Rev[7:0] is per-product ("Value is shown in
       product sheet") and PR31500.PDF does not state it; nk.exe issues no
       MFC0 from r15, so the field is unobservable to this ROM. */
    uint32_t     Prid()     const override { return 0x00002200u; }

    /* 32 entries: Index holds 5 bits and Random is constrained to "the TLB
       indexes from 31 to 8" (TMPR3911.pdf Fig 3.3.8, Fig 3.3.9). */
    uint32_t     TlbSize()  const override { return 32u; }

    /* 4-KB page: EntryLo PFN is "Bits 31..12 of the physical address"
       (TMPR3911.pdf Fig 3.3.6). */
    uint32_t     MinPageShift() const override { return 12u; }

    /* Every band of the 4-GB physical map is separately assigned, and a TLB PFN
       reaches each one unmasked (TMPR3911.pdf Table 4.2.1). */
    uint32_t     PhysAddrMask() const override { return 0xFFFFFFFFu; }

    /* "MIPS R3000 core" (PR31500.PDF p.2). */
    MipsIsaLevel IsaLevel() const override { return MipsIsaLevel::kMips1; }

    /* "The IRQHIGH signal is connected to interrupt bit 4 on the TX39/H Processor
       Core and the IRQLOW signal is connected to interrupt bit 2" (TMPR3911.pdf
       ch.8). Cause IP[5:0] occupies bits 15:10 (TMPR39xx-um Fig 6-2), so IRQHIGH
       is bit 14 and IRQLOW is bit 12. */
    uint32_t     DeviceIpMask() const override { return 0x00005000u; }

    /* No FPU: the PR31500 core block carries only R3000 CPU, TLB, I-Cache and
       D-Cache (PR31500.PDF Figure 2). */
    bool HasFpu()     const override { return false; }
    /* LL/SC is a MIPS-II instruction pair and the TX39 family is ISA I; CP0 has
       no LL-address register (TMPR39xx-um Table 2-1 lists r17 as DEPC). */
    bool HasLlsc()    const override { return false; }
    /* Count (r9) and Compare (r11) are "(reserved)" (TMPR39xx-um Table 2-1). */
    bool HasCounter() const override { return false; }
    /* r18-31 are "(reserved)" (TMPR39xx-um Table 2-1). */
    bool HasWatch()   const override { return false; }
    /* Power-down is a CPU clock-stop mode (PR31500.PDF p.2). */
    bool HasVr41xxPowerModes() const override { return false; }
    /* No MIPS16 on the R3900 core (TMPR39xx-um documents none). */
    bool HasMips16() const override { return false; }
};

}  // namespace

REGISTER_SERVICE_AS(Pr31500ProcessorConfig, MipsProcessorConfig);
