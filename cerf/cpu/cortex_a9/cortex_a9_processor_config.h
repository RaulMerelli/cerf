#pragma once

#include "../arm_processor_config.h"

/* Cortex-A9 core invariants shared by i.MX6 variants. SoC-specific
   configuration supplies MIDR, cache topology, clocks and timer ratios. */
class CortexA9ProcessorConfigBase : public ArmProcessorConfig {
public:
    using ArmProcessorConfig::ArmProcessorConfig;

    /* ARM DDI 0406C.c PCStoreValue() (p. A2-47): the +12 alternative is
       permitted only before ARMv7. */
    uint32_t PcStoreOffset()              const override { return 8; }
    bool     BaseRestoredAbortModel()     const override { return true; }
    uint32_t CacheLineSize()              const override { return 32; }

    /* Cortex-A9 CTR: ARMv7 format, 32-byte I/D minimum line, VIPT I-cache. */
    uint32_t Ctr() const override { return 0x80038003u; }

    bool HasDsp()                     const override { return true; }
    bool HasThumb2()                  const override { return true; }
    bool HasLoadStoreDouble()         const override { return true; }
    bool HasPreload()                 const override { return true; }
    bool HasClz()                     const override { return true; }
    bool HasBlxReg()                  const override { return true; }
    bool HasArmv5UnconditionalSpace() const override { return true; }
    bool HasLoadToPcInterworking()    const override { return true; }
    bool HasDataProcToPcInterworking() const override { return true; }
    bool HasMls()                     const override { return true; }
    bool HasMovwMovt()                const override { return true; }
    bool HasBitField()                const override { return true; }
    bool HasRev()                     const override { return true; }
    bool HasExtendRotate()            const override { return true; }
    bool HasLdrexStrex()              const override { return true; }
    bool HasBarrierInsn()             const override { return true; }
    bool HasCp15V6()                  const override { return true; }
    bool HasCp15V7()                  const override { return true; }
    bool HasVmsav7()                  const override { return true; }
    bool HasSecurityExtensions()      const override { return true; }
    bool HasL2CacheAuxControl()       const override { return true; }
    bool HasAuxControlRegister()      const override { return true; }

    bool HasVfp()  const override { return true; }
    bool HasNeon() const override { return true; }
    uint32_t Fpsid() const override { return 0x41033090u; }
    uint32_t Mvfr0() const override { return 0x11110222u; }
    uint32_t Mvfr1() const override { return 0x00011111u; }
};
