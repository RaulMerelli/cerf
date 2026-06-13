#pragma once

#include "../arm_processor_config.h"

/* Cortex-A8 core invariants identical across every Cortex-A8 SoC. Per-SoC
   concretes override only MIDR, CCSIDR/CLIDR, CpuClockHz, and the timer
   dividers — a per-part value placed here is reported by every Cortex-A8 SoC. */
class CortexA8ProcessorConfigBase : public ArmProcessorConfig {
public:
    using ArmProcessorConfig::ArmProcessorConfig;

    uint32_t PcStoreOffset()              const override { return 12; }
    bool     BaseRestoredAbortModel()     const override { return true; }
    bool     MemoryBeforeWritebackModel() const override { return true; }
    bool     GenerateSyscalls()           const override { return false; }
    uint32_t CacheLineSize()              const override { return 64; }

    /* Cortex-A8 Cache Type Register reset value (ARM DDI0344K TRM, c0 Cache
       Type Register, page 3-20). */
    uint32_t Ctr()                        const override { return 0x82048004u; }

    bool     HasDsp()                     const override { return true; }
    bool     HasLoadStoreDouble()         const override { return true; }
    bool     HasClz()                     const override { return true; }
    bool     HasBlxReg()                  const override { return true; }
    bool     HasArmv5UnconditionalSpace() const override { return true; }

    /* v5T+ load-to-PC and v7 data-proc-to-PC interworking (DDI0406C §A2.3.1). */
    bool     HasLoadToPcInterworking()     const override { return true; }
    bool     HasDataProcToPcInterworking() const override { return true; }

    bool     HasMovwMovt()                const override { return true; }
    bool     HasBitField()                const override { return true; }
    bool     HasRev()                     const override { return true; }
    bool     HasExtendRotate()            const override { return true; }
    bool     HasLdrexStrex()              const override { return true; }
    bool     HasBarrierInsn()             const override { return true; }
    bool     HasCp15V6()                  const override { return true; }
    bool     HasCp15V7()                  const override { return true; }
    bool     HasVmsav7()                  const override { return true; }
    bool     HasL2CacheAuxControl()       const override { return true; }

    bool     HasVfp()  const override { return true; }
    bool     HasNeon() const override { return true; }
    uint32_t Fpsid()   const override { return 0x410330C0u; }
    uint32_t Mvfr0()   const override { return 0x11110222u; }
    uint32_t Mvfr1()   const override { return 0x00011111u; }
};
