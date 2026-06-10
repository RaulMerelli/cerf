#pragma once

#include "../mediaq_ge/mediaq_ge.h"

#include <cstdint>

/* MQ-1100/1132 (Falcon) instance of the shared MediaQ 2D engine: MQ-1132
   register indices, solid-fill encoding, and Source-FIFO packing only. */
class MediaQMq1188Ge : public MediaQGe {
public:
    using MediaQGe::MediaQGe;

    /* CC01R (Reg 4-10): CFS[4:0]=0x10 command FIFO empty, SFS[12:8]=0x10 source
       FIFO empty, bit16 GE-busy=0. Synchronous execution => always ready. */
    uint32_t StatusReady() const override { return 0x10u | (0x10u << 8); }

protected:
    const Layout& Lyt() const override;
    bool          IsSolidFill(uint32_t cmd) const override;
    uint32_t      SolidFillColor() const override;
    uint32_t      LineColor(uint32_t cmd) const override;
    uint32_t      ExpectedSourceDwords() const override;
    void          BlitColorSource(const uint32_t* r) override;
    void          BlitMonoSource(const uint32_t* r) override;

private:
    /* MQ-1132 GE register dword indices (Table 4-11). */
    enum : uint32_t {
        kGe07FgColor   = 7,   /* foreground, mono source (Reg 4-93). */
        kGe08BgColor   = 8,   /* background, mono source (Reg 4-94). */
        kGe09SrcStride = 9,   /* source stride / pack-mode (Reg 4-95). */
        kGe12PatFg     = 18,  /* solid-pattern colour (Reg 4-106). */
    };
    /* MQ-1132 selects a flat fill via the solid-pattern bit (Reg 4-83 [30]). */
    static constexpr uint32_t kCmdSolidPat = 1u << 30;
};
