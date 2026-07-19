#include "../mips_processor_config.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"

namespace {

class Vr4122ProcessorConfig : public MipsProcessorConfig {
public:
    using MipsProcessorConfig::MipsProcessorConfig;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4122;
    }

    /* PRId (CP0 r15): Imp 0x0C | Rev 0x70 (Linux arch/mips/include/asm/cpu.h
       PRID_IMP_VR41XX 0x0c00 | PRID_REV_VR4122 0x0070). */
    uint32_t     Prid()     const override { return 0x00000C70u; }

    /* Full-associative JTLB, 32 entries (U15509EJ2V0UM 1.5.1). */
    uint32_t     TlbSize()  const override { return 32u; }

    /* Min TLB page 1 KB -> shift 10 (U15509EJ2V0UM 1.5.1: 1 KB..256 KB). */
    uint32_t     MinPageShift() const override { return 10u; }

    /* PA above 0x1FFFFFFF mirrors 0x00000000-0x1FFFFFFF (VR4131 UM U15350EJ2V0UM Fig 3-1). */
    uint32_t     PhysAddrMask() const override { return 0x1FFFFFFFu; }

    /* MIPS I/II/III; no MIPS IV (U15509EJ2V0UM Table 1-3, VR4122 column). */
    MipsIsaLevel IsaLevel() const override { return MipsIsaLevel::kMips3; }

    /* ICU drives Int0/Int1/Int2 (+NMI) only -> Cause IP2/IP3/IP4 = bits 10-12; no
       Int3/HSP this generation (VR4131 UM U15350EJ2V0UM 11.1, p.187-188). */
    uint32_t     DeviceIpMask() const override { return 0x00001C00u; }

    /* No FPU (U15509EJ2V0UM 1.7, Table 1-3 Floating-point = N/A). */
    bool HasFpu()     const override { return false; }
    /* No LL bit; LL/LLD/SC/SCD unsupported (U15509EJ2V0UM 1.7, Table 1-3). */
    bool HasLlsc()    const override { return false; }
    /* CP0 Count r9 + Compare r11 present (U15509EJ2V0UM Table 1-2). */
    bool HasCounter() const override { return true; }
    /* CP0 WatchLo r18 + WatchHi r19 present (U15509EJ2V0UM Table 1-2). */
    bool HasWatch()   const override { return true; }
    /* HIBERNATE/STANDBY/SUSPEND added in the VR4100 core (U15509EJ2V0UM 1.7, Table 1-3). */
    bool HasVr41xxPowerModes() const override { return true; }
    /* MIPS16 ASE (U15509EJ2V0UM ch.3); MIPS16EN strapped active on the EM-500 -
       nk_main_kernel.exe executes JALX 0x77C240F7 @0x9F033DBC. */
    bool HasMips16() const override { return true; }
};

}  // namespace

REGISTER_SERVICE_AS(Vr4122ProcessorConfig, MipsProcessorConfig);
