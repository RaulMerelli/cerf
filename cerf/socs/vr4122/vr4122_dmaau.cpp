#include "vr4122_reg_window_impl.h"

#include <cstdint>

namespace {

using cerf_vr4122_reg_window_detail::Vr4122RegWindowBase;
using cerf_vr4122_reg_window_detail::Vr4122RegWindowModel;

/* CSI + FIR window 0x0F000020-0x37 (VR4131 UM Table 8-1, p162). Write masks + reset
   values per register: CSIIBAL/H 8.2.1 p163, CSIIAL/H 8.2.2 p164, CSIOBAL/H 8.2.3
   p165, CSIOAL/H 8.2.4 p166, FIRBAL/H 8.2.5 p167, FIRAL/H 8.2.6 p168. Boot writes
   FIRBAL/H at nk.exe 0x9F0338EC (sw 0xA0003800 -> 0x0F000030). */
constexpr Vr4122RegWindowModel kModel = {
    /*base=*/0x0F000020u,
    /*size=*/0x20u,
    /*num_regs=*/12u,
    /*wmask=*/{
        0xFFFCu, 0x07FFu,   /* CSIIBAL 15:2 / CSIIBAH 10:0   */
        0x07FCu, 0x0000u,   /* CSIIAL 10:2   / CSIIAH R      */
        0xFFFFu, 0x07FFu,   /* CSIOBAL 15:0  / CSIOBAH 10:0  */
        0x07FCu, 0x0000u,   /* CSIOAL 10:2   / CSIOAH R      */
        0xFFFFu, 0x07FFu,   /* FIRBAL 15:0   / FIRBAH 10:0   */
        0x0FFFu, 0x0000u,   /* FIRAL 11:0    / FIRAH R       */
    },
    /*reset=*/{
        0xF800u, 0x01FFu, 0xF800u, 0x01FFu,
        0xF800u, 0x01FFu, 0xF800u, 0x01FFu,
        0xF800u, 0x01FFu, 0xF800u, 0x01FFu,
    },
    /*fatal_on_set=*/{},
};

class Vr4122Dmaau : public Vr4122RegWindowBase<kModel> {
public:
    using Vr4122RegWindowBase::Vr4122RegWindowBase;
};

}  /* namespace */

REGISTER_SERVICE(Vr4122Dmaau);
