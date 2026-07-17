#include "../vr41xx/vr41xx_icu_impl.h"

#include <cstdint>

namespace {

using cerf_vr41xx_icu_detail::Vr41xxIcuBase;
using cerf_vr41xx_icu_detail::Vr41xxIcuModel;

/* VR4102 ICU (UM Table 14-1): SYSINT1REG..SOFTINTREG at 0x0B000080-0x0B00009A and
   SYSINT2REG..MFIRINTREG at 0x0B000200-0x0B00020A. The PMU block follows the first
   window at 0x0B0000A0 (UM Table 15-4), so it decodes 0x0B000080-9F. */
constexpr Vr41xxIcuModel kModel = {
    /*base1=*/0x0B000080u,
    /*size1=*/0x20u,
    /*base2=*/0x0B000200u,
    /*size2=*/0x10u,
    /* SYSINT1REG bits with no Level-2 register (UM 14.2.1): D13 DOZEPIUINTR,
       D10 WRBERRINTR, D9 SIUINTR, D3 ETIMERINTR, D2 RTCL1INTR, D1 POWERINTR,
       D0 BATINTR. D11 SOFTINTR is NOT one - it is computed from SOFTINTREG. */
    /*s1_direct=*/0x260Fu,
    /* SYSINT2REG bits with no Level-2 register (UM 14.2.15): D3 TCLKINTR, D2 HSPINTR,
       D1 LEDINTR, D0 RTCL2INTR. */
    /*s2_direct=*/0x000Fu,
};

class Vr4102Icu : public Vr41xxIcuBase<SocFamily::VR4102, kModel> {
public:
    using Vr41xxIcuBase::Vr41xxIcuBase;
};

}  /* namespace */

REGISTER_SERVICE_AS(Vr4102Icu, Vr41xxIcu);
