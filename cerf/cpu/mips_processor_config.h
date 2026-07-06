#pragma once

#include <cstdint>

#include "../core/service.h"
#include "../jit/mips/mips_cpu_state.h"

/* MIPS ISA level a SoC's core implements (Linux arch/mips cpu.h MIPS_CPU_ISA_*).
   One value today (VR5500 = MIPS IV); concretes select theirs as more land. */
enum class MipsIsaLevel : uint32_t {
    kMips3,   /* MIPS_CPU_ISA_III */
    kMips4,   /* MIPS_CPU_ISA_IV */
};

/* Per-SoC MIPS processor identity/capability strategy, MIPS analog of
   ArmProcessorConfig. One concrete per MIPS SoC, selected by GetSoc(). */
class MipsProcessorConfig : public Service {
public:
    using Service::Service;

    /* CP0 $15 (PRId) reset value: company<<16 | implementation<<8 | revision.
       Seeded into MipsCpuState::cp0_prid at reset. */
    virtual uint32_t Prid() const = 0;

    /* Joint-TLB live entry count (QEMU nb_tlb). Seeds MipsCpuState::nb_tlb. */
    virtual uint32_t TlbSize() const = 0;

    /* ISA level the decoder must implement for this SoC (asserted at reset). */
    virtual MipsIsaLevel IsaLevel() const = 0;

    /* Silicon capability flags - the cpuinfo_mips option set (Linux arch/mips
       cpu.h MIPS_CPU_*), MIPS mirror of ArmProcessorConfig's HasX(). */
    virtual bool HasFpu()     const = 0;   /* MIPS_CPU_FPU - CP1 present (gates COP1 decode) */
    virtual bool HasLlsc()    const = 0;   /* MIPS_CPU_LLSC - gates LL/SC decode */
    virtual bool HasCounter() const = 0;   /* MIPS_CPU_COUNTER - CP0 Count/Compare present */
    virtual bool HasWatch()   const = 0;   /* MIPS_CPU_WATCH - CP0 WatchLo/WatchHi present */

    /* Whether this SoC implements CP0 register `rd` - gates the config-dependent
       registers (Count/Compare, WatchLo/WatchHi); always-present registers
       return true and are further validated by Cp0RegOffset. Consumed by the
       MFC0/MTC0 place fns. */
    bool HasCp0Reg(uint32_t rd) const {
        if (rd == MipsCp0::kCount   || rd == MipsCp0::kCompare) return HasCounter();
        if (rd == MipsCp0::kWatchLo || rd == MipsCp0::kWatchHi) return HasWatch();
        return true;
    }
};
