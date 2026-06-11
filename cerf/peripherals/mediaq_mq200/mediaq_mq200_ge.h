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
    void          DrawLine(uint32_t cmd) override;

private:
    enum : uint32_t {
        kGe07FgColor  = 0x07,  /* FG_COLOR: mono-source / solid-line foreground. */
        kGe08BgColor  = 0x08,  /* BG_COLOR: mono-source background. */
        kGe09SrcOff   = 0x09,  /* GE09R source stride / pack-mode. */
        kGe40MonoPat0 = 0x40,
        kGe41MonoPat1 = 0x41,
        kGe42PatFg    = 0x42,  /* PAT_FG_COLOR: mono-pattern / solid-fill colour. */
        kGe43PatBg    = 0x43,  /* PAT_BG_COLOR: mono-pattern background. */
    };
};
