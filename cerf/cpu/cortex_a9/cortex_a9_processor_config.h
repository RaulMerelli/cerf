#pragma once

#include "../arm_processor_config.h"

class CortexA9ProcessorConfigBase : public ArmProcessorConfig {
public:
    using ArmProcessorConfig::ArmProcessorConfig;

    /* ARM DDI 0406C.c PCStoreValue() (p. A2-47): the +12 alternative is
       permitted only before ARMv7. */
    uint32_t PcStoreOffset() const override { return 8; }
    bool BaseRestoredAbortModel() const override { return true; }
    uint32_t CacheLineSize() const override { return 32; }

    /* ARM DDI 0388I Table 4-16. */
    uint32_t Ctr() const override { return 0x83338003u; }

    bool HasDsp() const override { return true; }
    bool HasThumb2() const override { return true; }
    bool HasLoadStoreDouble() const override { return true; }
    bool HasPreload() const override { return true; }
    bool HasClz() const override { return true; }
    bool HasBlxReg() const override { return true; }
    bool HasArmv5UnconditionalSpace() const override { return true; }
    bool HasLoadToPcInterworking() const override { return true; }
    bool HasDataProcToPcInterworking() const override { return true; }
    bool HasMls() const override { return true; }
    bool HasMovwMovt() const override { return true; }
    bool HasBitField() const override { return true; }
    bool HasRev() const override { return true; }
    bool HasExtendRotate() const override { return true; }
    bool HasLdrexStrex() const override { return true; }
    bool HasBarrierInsn() const override { return true; }
    bool HasCp15V6() const override { return true; }
    bool HasCp15V7() const override { return true; }
    bool HasVmsav7() const override { return true; }
    bool HasSecurityExtensions() const override { return true; }
    bool HasL2CacheAuxControl() const override { return true; }
    bool HasAuxControlRegister() const override { return true; }

    bool HasVfp() const override { return true; }
    bool HasNeon() const override { return true; }
    /* QEMU v11.0.0 target/arm/tcg/cpu32.c:cortex_a9_initfn. */
    uint32_t Fpsid() const override { return 0x41033090u; }
    uint32_t Mvfr0() const override { return 0x11110222u; }
    uint32_t Mvfr1() const override { return 0x01111111u; }
};
