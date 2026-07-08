#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>
#include <mutex>

/* NEC VR4102 ICU (Interrupt Control Unit), UM ch.14 Table 14-1 / Fig 14-1.
   Two-level: Level-2 xxxINTREG & MxxxINTREG collapse into SYSINT1/2REG, which
   & MSYSINT drive MIPS Cause.IP2..IP5. Owns two MMIO blocks: 0x0B000080, 0x0B000200. */
class Vr4102Icu : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x0B000080u; }  /* ICU1 block */
    uint32_t MmioSize() const override { return 0x20u; }        /* 0x0080-0x009F */

    uint16_t ReadHalf(uint32_t addr) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("ICU ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("ICU ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("ICU WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("ICU WriteWord", addr, v); }

    /* ICU2 block (0x0B000200) accessors, driven by the Vr4102Icu2Mmio adapter. */
    uint16_t ReadHalf2(uint32_t off);
    void     WriteHalf2(uint32_t off, uint16_t value);

    /* Source assertion. Refreshes SYSINT + the CPU Int level. Thread-safe
       (peripheral threads). Direct SYSINT sources set a SYSINT1/SYSINT2 bit; the
       GIU pushes its whole per-pin interrupt indication as GIUINTL/GIUINTH. */
    void SetSysint1Source(uint16_t bit, bool level);
    void SetSysint2Source(uint16_t bit, bool level);
    void SetGiuLow(uint16_t bits);
    void SetGiuHigh(uint16_t bits);
    void SetPiuSource(uint16_t bits);
    void SetKiuSource(uint16_t bits);

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    mutable std::mutex mtx_;

    /* Level-2 indication registers (R) set by their source unit + masks (R/W). */
    uint16_t giuintl_ = 0, giuinth_ = 0, piuint_ = 0, aiuint_ = 0,
             kiuint_ = 0, dsiuint_ = 0, firint_ = 0;
    uint16_t mgiul_ = 0, mgiuh_ = 0, mpiu_ = 0, maiu_ = 0,
             mkiu_ = 0, mdsiu_ = 0, mfir_ = 0;

    /* Level-1 direct-source bits (the SYSINT1/2 bits with no Level-2 register)
       + Level-1 masks + NMIREG + SOFTINTREG. */
    uint16_t sysint1_direct_ = 0, sysint2_direct_ = 0;
    uint16_t msysint1_ = 0, msysint2_ = 0, nmireg_ = 0, softint_ = 0;

    uint16_t ComputeSysint1Locked() const;
    uint16_t ComputeSysint2Locked() const;
    void     RecomputeLocked();
};
