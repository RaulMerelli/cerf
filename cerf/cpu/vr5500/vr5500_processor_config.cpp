#include "../mips_processor_config.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"

namespace {

/* NEC VR5500 (MIPS IV, R4000-class, little-endian). Values = the Linux
   arch/mips R5500 cpu-probe case (cpu.h MIPS_CPU_* / PRID_IMP_R5500). */
class Vr5500ProcessorConfig : public MipsProcessorConfig {
public:
    using MipsProcessorConfig::MipsProcessorConfig;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR5500;
    }

    /* company<<16(=0, legacy NEC) | PRID_IMP_R5500(0x5500) | revision(0). */
    uint32_t     Prid()     const override { return 0x00005500u; }
    uint32_t     TlbSize()  const override { return 48u; }   /* R5500 c->tlbsize */
    uint32_t     MinPageShift() const override { return 12u; }  /* R4000/R5000 4-KB min page */
    uint32_t     PhysAddrMask() const override { return 0xFFFFFFFFu; }  /* no low-space mirror */
    MipsIsaLevel IsaLevel() const override { return MipsIsaLevel::kMips4; }

    bool HasFpu()     const override { return true;  }   /* MIPS_CPU_FPU */
    bool HasCounter() const override { return true;  }   /* MIPS_CPU_COUNTER */
    bool HasWatch()   const override { return true;  }   /* MIPS_CPU_WATCH */
    bool HasLlsc()    const override { return true;  }   /* MIPS_CPU_LLSC */
    bool HasVr41xxPowerModes() const override { return false; }
};

}  /* namespace */

REGISTER_SERVICE_AS(Vr5500ProcessorConfig, MipsProcessorConfig);
