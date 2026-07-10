#include "../mips_processor_config.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"

namespace {

/* Philips PR31700, R3000A-based PR3901 core (PR31700.PDF p.2). The TLB and CP0
   registers are the Toshiba TX39 family's: TMPR3911.pdf ch.3, TMPR39xx-um ch.2. */
class Pr31700ProcessorConfig : public MipsProcessorConfig {
public:
    using MipsProcessorConfig::MipsProcessorConfig;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::PR31700;
    }

    /* nk.exe 0x9F4116A8 compares the whole PRId against 0x2202 and only then
       programs VIDEO_BAUDVAL = 7, which sets the panel's VIDRATE. */
    uint32_t     Prid()     const override { return 0x00002202u; }

    /* 32 entries: Index holds 5 bits and Random is constrained to "the TLB
       indexes from 31 to 8" (TMPR3911.pdf Fig 3.3.8, Fig 3.3.9). */
    uint32_t     TlbSize()  const override { return 32u; }

    /* 4-KB page: EntryLo PFN is "Bits 31..12 of the physical address"
       (TMPR3911.pdf Fig 3.3.6). */
    uint32_t     MinPageShift() const override { return 12u; }

    /* Every band of the 4-GB physical map is separately assigned, and a TLB PFN
       reaches each one unmasked (TMPR3911.pdf Table 4.2.1). */
    uint32_t     PhysAddrMask() const override { return 0xFFFFFFFFu; }

    /* "R3000A-based PR3901 Processor Core" (PR31700.PDF p.2). */
    MipsIsaLevel IsaLevel() const override { return MipsIsaLevel::kMips1; }

    /* "The IRQHIGH signal is connected to interrupt bit 4 on the TX39/H Processor
       Core and the IRQLOW signal is connected to interrupt bit 2" (TMPR3911/3912
       ch.8, p.8-4). Cause IP[5:0] occupies bits 15:10 (TMPR39xx-um Fig 6-2), so
       Int4 is bit 14 and Int2 is bit 12. */
    uint32_t     DeviceIpMask() const override { return 0x00005000u; }

    /* No CP1: the PR31700 feature list has no FPU (PR31700.PDF p.2) and the
       R3900 core's CP0 map has no CP1 (TMPR39xx-um Table 2-1). */
    bool HasFpu()     const override { return false; }
    /* The TX39 has no Load Linked / Store Conditional instruction and no CP0
       LL-address register (TMPR39xx-um Table 2-1). */
    bool HasLlsc()    const override { return false; }
    /* Count (r9) and Compare (r11) are reserved (TMPR39xx-um Table 2-1). */
    bool HasCounter() const override { return false; }
    /* r18-31 are reserved (TMPR39xx-um Table 2-1). */
    bool HasWatch()   const override { return false; }
    /* Power-down is a CPU clock-stop mode (PR31700.PDF p.2). */
    bool HasVr41xxPowerModes() const override { return false; }
};

}  // namespace

REGISTER_SERVICE_AS(Pr31700ProcessorConfig, MipsProcessorConfig);
