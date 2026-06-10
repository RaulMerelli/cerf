#pragma once

#include "../mediaq_ge/mediaq_ge.h"

#include <cstdint>

/* MQ-200 (SIMpad SL4) instance of the shared MediaQ 2D engine: MQ-200 register
   indices, solid-fill encoding, and Source-FIFO packing only (ddi.dll
   sub_13458A0 fill, sub_1346EF8 colour source). */
class MediaQMq200Ge : public MediaQGe {
public:
    using MediaQGe::MediaQGe;

    /* CC01R Source/Command FIFO + GE status (Data Book Table 5-86): CFS[4:0]=
       0x10 command FIFO empty, SFS[11:8]=0x8 source FIFO empty, bit16 busy=0. */
    uint32_t StatusReady() const override { return 0x10u | (0x8u << 8); }

protected:
    const Layout& Lyt() const override;
    bool          IsSolidFill(uint32_t cmd) const override;
    uint32_t      SolidFillColor() const override;
    uint32_t      LineColor(uint32_t cmd) const override;
    uint32_t      ExpectedSourceDwords() const override;
    void          BlitColorSource(const uint32_t* r) override;
    void          BlitMonoSource(const uint32_t* r) override;

private:
    /* MQ-200 GE register dword indices. Mono/pattern colours live in the GE40
       block (Data Book §5 register locator), not the GE07/08 of the MQ-1132. */
    enum : uint32_t {
        kGe08FgColor = 0x08,  /* GE08R colour foreground / rectangle fill. */
        kGe09SrcOff  = 0x09,  /* GE09R source stride / pack-mode. */
        kGe42PatFg   = 0x42,  /* GE42R mono pattern / mono source foreground. */
        kGe43PatBg   = 0x43,  /* GE43R mono pattern / mono source background. */
    };
};
