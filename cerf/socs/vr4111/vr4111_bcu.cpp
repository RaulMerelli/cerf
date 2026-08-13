#include "../vr41xx/vr41xx_reg_window_impl.h"

#include <cstdint>

namespace {

using cerf_vr41xx_reg_window_detail::OtherReset;
using cerf_vr41xx_reg_window_detail::ReadKind;
using cerf_vr41xx_reg_window_detail::Vr41xxRegWindowBase;
using cerf_vr41xx_reg_window_detail::Vr41xxRegWindowModel;
using cerf_vr41xx_reg_window_detail::WriteKind;

/* BCU 0x0B000000-0x0B00001F, DMAA follows at 0x0B000020 (VR4111 UM Table 6-10 p170);
   register offsets from UM Table 11-1 p263. */
constexpr Vr41xxRegWindowModel kModel = {
    /*base=*/0x0B000000u,
    /*size=*/0x20u,
    /*num_regs=*/12u,
    /*word_pairs=*/false,
    {
        /* 0x00 BCUCNTREG1 (UM 11.2.1 p264): R/W D15/14/13/12/10/8/6/4/1/0,
           RFU-read-0 D11/9/7/5/3/2, RTCRST and Other-resets rows both 0. */
        { ReadKind::kStored, WriteKind::kStored, 0xF553u, 0x0000u, 0u },
        /* 0x02 BCUCNTREG2 (UM 11.2.2 p266): R/W D0 GMODE, D15:1 RFU-read-0,
           both reset rows 0. */
        { ReadKind::kStored, WriteKind::kStored, 0x0001u, 0x0000u, 0u },
        {},
        {},
        {},
        /* 0x0A BCUSPEEDREG (UM 11.2.3 p267): R/W D13:12 WPROM, D10:8 WLCD/M,
           D6:4 WISAA, D2:0 WROMA, RFU-read-0 D15/14/11/7/3, both rows 0. */
        { ReadKind::kStored, WriteKind::kStored, 0x3777u, 0x0000u, 0u },
        {},
        /* 0x0E BCURFCNTREG (UM 11.2.5 p270): R/W D13:0 BRF, RFU-read-0 D15:14;
           RTCRST 0x0200; Other resets 0 on D15:14, Undefined on D13:0. */
        { ReadKind::kStored, WriteKind::kStored, 0x3FFFu, 0x0200u, 0u,
          OtherReset::kReset, 0x3FFFu },
    },
};

static_assert((0xF553u & 0x0AACu) == 0u && (0xF553u | 0x0AACu) == 0xFFFFu,
              "BCUCNTREG1 writable and RFU-read-0 bits must partition all 16");
static_assert((0x0001u & 0xFFFEu) == 0u && (0x0001u | 0xFFFEu) == 0xFFFFu,
              "BCUCNTREG2 writable and RFU-read-0 bits must partition all 16");
static_assert((0x3777u & 0xC888u) == 0u && (0x3777u | 0xC888u) == 0xFFFFu,
              "BCUSPEEDREG writable and RFU-read-0 bits must partition all 16");
static_assert((0x3FFFu & 0xC000u) == 0u && (0x3FFFu | 0xC000u) == 0xFFFFu,
              "BCURFCNTREG writable and RFU-read-0 bits must partition all 16");

class Vr4111Bcu : public Vr41xxRegWindowBase<SocFamily::VR4111, kModel> {
public:
    using Vr41xxRegWindowBase::Vr41xxRegWindowBase;
};

}

REGISTER_SERVICE(Vr4111Bcu);
