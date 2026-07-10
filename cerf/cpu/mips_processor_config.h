#pragma once

#include <cstdint>

#include "../core/service.h"
#include "../jit/mips/mips_cpu_state.h"

/* MIPS ISA level a SoC's core implements (Linux arch/mips cpu.h MIPS_CPU_ISA_*).
   One value today (VR5500 = MIPS IV); concretes select theirs as more land. */
enum class MipsIsaLevel : uint32_t {
    kMips1,
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

    /* log2(min TLB page size): VA offset bits a PFN leaves untranslated
       (R4000/R5000 4 KB->12; VR4100 1 KB->10, VR4102 UM 5.2.2). Seeds
       MipsCpuState::min_page_shift. */
    virtual uint32_t MinPageShift() const = 0;

    /* AND-mask applied to a TLB-translated physical address for the SoC's
       physical-space aliasing: the VR4102 mirrors PA 0-0x1FFFFFFF across the
       whole space above 0x20000000 (UM Table 5-6) -> mask 0x1FFFFFFF; a SoC with
       no such mirror returns 0xFFFFFFFF. Seeds MipsCpuState::phys_addr_mask. */
    virtual uint32_t PhysAddrMask() const = 0;

    /* ISA level the decoder must implement for this SoC (asserted at reset). */
    virtual MipsIsaLevel IsaLevel() const = 0;

    /* Cause.IP bits the SoC's interrupt controller drives. */
    virtual uint32_t DeviceIpMask() const = 0;

    /* Silicon capability flags - the cpuinfo_mips option set (Linux arch/mips
       cpu.h MIPS_CPU_*), MIPS mirror of ArmProcessorConfig's HasX(). */
    virtual bool HasFpu()     const = 0;   /* MIPS_CPU_FPU - CP1 present (gates COP1 decode) */
    virtual bool HasLlsc()    const = 0;   /* MIPS_CPU_LLSC - gates LL/SC decode */
    virtual bool HasCounter() const = 0;   /* MIPS_CPU_COUNTER - CP0 Count/Compare present */
    virtual bool HasWatch()   const = 0;   /* MIPS_CPU_WATCH - CP0 WatchLo/WatchHi present */

    /* STANDBY/SUSPEND/HIBERNATE (COP0 CO=1, funct 0x21/0x22/0x23). VR4102 UM
       ch.27: "added in the VR4100 CPU core"; absent from MIPS I-IV, so a core
       outside that family decodes them as reserved. */
    virtual bool HasVr41xxPowerModes() const = 0;

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
