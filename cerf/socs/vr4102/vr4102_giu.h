#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>
#include <mutex>

/* NEC VR4102 GIU (General Purpose I/O Unit), 0x0B000100-0x0B00011F (UM ch.18):
   GPIO direction/output-data plus the SoC's Level-2 GPIO interrupt block. A pin
   input event (SetPinLevel) is evaluated against the per-pin trigger config and,
   when enabled, latched into GIUINTSTAT and pushed to the ICU as GIUINTL/GIUINTH. */
class Vr4102Giu : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x0B000100u; }
    uint32_t MmioSize() const override { return 0x20u; }

    uint16_t ReadHalf(uint32_t addr) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;
    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("GIU ReadByte", addr, 0); }
    void     WriteByte(uint32_t addr, uint8_t v) override { HaltUnsupportedAccess("GIU WriteByte", addr, v); }

    /* A board GPIO source drives pin `pin` (0..31) to `level` here; the GIU
       evaluates the configured trigger and drives its interrupt to the ICU. */
    void SetPinLevel(int pin, bool level);

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    uint16_t ReadHalfLocked(uint32_t off);
    void     WriteHalfLocked(uint32_t off, uint16_t value);
    void     DriveIcuLocked();

    mutable std::mutex mtx_;

    uint16_t iosel_l_ = 0;          /* GIUIOSELL direction (1=out/0=in) */
    uint16_t podat_l_ = 0xFFFFu;    /* GIUPODATL output data, reset all-high */

    /* GIUPIODL/H GPIO data (pins [31:0]): output pins latch the driven data,
       input pins read level_. iosel_h_ (GIUIOSELH) is unreached over MMIO but
       its reset value 0 (all high pins input) drives the PIODH read. */
    uint16_t piod_out_l_ = 0, piod_out_h_ = 0;
    uint16_t iosel_h_    = 0;

    /* Level-2 GPIO interrupt block, low pins [15:0] + high pins [31:16]. */
    uint16_t intstat_l_ = 0, intstat_h_ = 0;   /* GIUINTSTAT pending (W1C) */
    uint16_t inten_l_   = 0, inten_h_   = 0;    /* GIUINTEN enable */
    uint16_t inttyp_l_  = 0, inttyp_h_  = 0;    /* GIUINTTYP 1=edge/0=level */
    uint16_t intalsel_l_= 0, intalsel_h_= 0;    /* GIUINTALSEL 1=high/0=low active */
    uint16_t inthtsel_l_= 0, inthtsel_h_= 0;    /* GIUINTHTSEL 1=hold/0=through */
    uint16_t retained_l_= 0, retained_h_= 0;    /* held change while INTEN=0 (UM 18.2.13) */
    uint32_t level_     = 0;                     /* current input level, pins [31:0] */
};
