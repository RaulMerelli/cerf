#include "vr4122_reg_window_impl.h"

#include <cstdint>

namespace {

using cerf_vr4122_reg_window_detail::Vr4122RegWindowBase;
using cerf_vr4122_reg_window_detail::Vr4122RegWindowModel;

/* RAM + I/O-space window 0x0F0001E0-0x1EF (VR4131 UM Table 8-1, p162). Write masks +
   reset values per register: RAMBAL/H 8.2.7 p169, RAMAL/H 8.2.8 p170, IOBAL/H 8.2.9
   p171 (IOBAH D11 "Write 1. 1 is returned"), IOAL/H 8.2.10 p172. */
constexpr Vr4122RegWindowModel kModel = {
    /*base=*/0x0F0001E0u,
    /*size=*/0x10u,
    /*num_regs=*/8u,
    /*wmask=*/{
        0xFFFCu, 0x07FFu,   /* RAMBAL 15:2 / RAMBAH 10:0 */
        0xFFFCu, 0x0003u,   /* RAMAL 15:2  / RAMAH 1:0   */
        0xFFFCu, 0x07FFu,   /* IOBAL 15:2  / IOBAH 10:0  */
        0xFFFCu, 0x07FFu,   /* IOAL 15:2   / IOAH 10:0   */
        0, 0, 0, 0, 0,
    },
    /*reset=*/{
        0xF800u, 0x01FFu,
        0xF800u, 0x01FFu,
        0x0000u, 0x0A00u,
        0x0000u, 0x0AFFu,
        0, 0, 0, 0, 0,
    },
    /*fatal_on_set=*/{},
};

class Vr4122DmaauRamIo : public Vr4122RegWindowBase<kModel> {
public:
    using Vr4122RegWindowBase::Vr4122RegWindowBase;
};

}  /* namespace */

REGISTER_SERVICE(Vr4122DmaauRamIo);
