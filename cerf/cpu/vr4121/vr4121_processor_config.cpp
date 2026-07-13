#include "../mips_processor_config.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"

namespace {

/* NEC VR4121 (uPD30121), VR4120 CPU core. Values from the VR4121 User's Manual
   (U13569EJ2V0UM00), per-accessor refs below. */
class Vr4121ProcessorConfig : public MipsProcessorConfig {
public:
    using MipsProcessorConfig::MipsProcessorConfig;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4121;
    }

    /* PRId (CP0 r15) UM Fig 6-18: [31:16] RFU reads 0, Imp[15:8] = 0x0C for the
       VR4121; Rev[7:0] = 0x60 identifies the VR4121 within Imp 0x0C
       (Linux arch/mips/include/asm/cpu.h PRID_IMP_VR41XX 0x0c00 /
       PRID_REV_VR4121 0x0060). */
    uint32_t     Prid()     const override { return 0x00000C60u; }

    /* Fully-associative on-chip TLB, 32 entries, odd/even page pairs (UM 6.1). */
    uint32_t     TlbSize()  const override { return 32u; }

    /* TLB page sizes are 1 K, 4 K, 16 K, 64 K, 256 K (UM 6.1). */
    uint32_t     MinPageShift() const override { return 10u; }

    /* PA above 0x20000000 is a mirror image of 0x00000000-0x1FFFFFFF (UM Fig 6-8). */
    uint32_t     PhysAddrMask() const override { return 0x1FFFFFFFu; }

    /* MIPS I/II/III; no MIPS IV (UM 5.6, Table 5-5). */
    MipsIsaLevel IsaLevel() const override { return MipsIsaLevel::kMips3; }

    /* Int0..Int3 -> Cause IP2..IP5, bits 10..13 (UM Fig 10-2). Int4/IP6 never
       occurs on this part; IP7 is the timer. */
    uint32_t     DeviceIpMask() const override { return 0x00003C00u; }

    /* "The VR4121 does not support the floating-point unit (FPU). Coprocessor
       Unusable exception will occur if any FPU instructions are executed" (UM 1.5.6). */
    bool HasFpu()     const override { return false; }
    /* "The VR4120 CPU core does not have the LL bit ... does not support
       instructions which manipulate the LL bit (LL, LLD, SC, SCD)" (UM 5.6);
       CP0 LLAddr (r17) is "Reserved for future use" (UM Table 1-18). */
    bool HasLlsc()    const override { return false; }
    /* CP0 Count (r9) + Compare (r11) present (UM Table 1-18). */
    bool HasCounter() const override { return true; }
    /* CP0 WatchLo (r18) + WatchHi (r19) present (UM Table 1-18). */
    bool HasWatch()   const override { return true; }
    /* HIBERNATE/STANDBY/SUSPEND added in the VR4120 CPU core (UM 5.6). */
    bool HasVr41xxPowerModes() const override { return true; }
};

}  // namespace

REGISTER_SERVICE_AS(Vr4121ProcessorConfig, MipsProcessorConfig);
