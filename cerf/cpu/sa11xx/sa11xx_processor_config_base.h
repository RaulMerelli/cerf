#pragma once

#include "../arm_processor_config.h"

struct DecodedInsn;

/* Common StrongARM SA-11x0 ARMv4 config (proc-sa1100.S:13: SA-1100 and
   SA-1110 share everything but CPU ID). Concretes supply Midr/Ctr + gate. */
class Sa11xxProcessorConfigBase : public ArmProcessorConfig {
public:
    using ArmProcessorConfig::ArmProcessorConfig;

    uint32_t PcStoreOffset()              const override { return 8; }

    /* proc-sa1100.S:238  dabort=v4_early_abort → base-RESTORED. */
    bool     BaseRestoredAbortModel()     const override { return true; }

    bool     MemoryBeforeWritebackModel() const override { return true; }
    bool     GenerateSyscalls()           const override { return false; }

    /* proc-sa1100.S:33  #define DCACHELINESIZE 32. */
    uint32_t CacheLineSize()              const override { return 32; }

    /* proc-sa1100.S:242 cpu_arch_name "armv4" — StrongARM has no Thumb. */
    bool     HasThumb()                   const override { return false; }
    bool     HasDsp()                     const override { return false; }
    bool     HasLoadStoreDouble()         const override { return false; }

    uint16_t CycleCostFor(const DecodedInsn& d) const override;

    /* 3686400 = OSCR base 3.6864 MHz (SA-1110 Dev Manual §9.4.1). Core clock
       is runtime PLL/PPCR-set, so CpuClockHz tracks the OST divider. */
    uint32_t CpuClockHz() const override { return CpuToOscrDivider() * 3686400u; }
};
