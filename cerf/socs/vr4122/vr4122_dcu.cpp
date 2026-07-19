#include "vr4122_reg_window_impl.h"

#include <cstdint>

namespace {

using cerf_vr4122_reg_window_detail::Vr4122RegWindowBase;
using cerf_vr4122_reg_window_detail::Vr4122RegWindowModel;

/* VR4131 UM U15350EJ2V0UM Table 9-2 p173; masks + reset columns 9.3.1-9.3.13
   p174-183 (RTCRST == After-reset; DMAIDLEREG resets to 1). "Setting the DRQIOR
   bit to 1 starts DMA transfer" (9.3.5 p177); TCINT "is transmitted as BCUINT to
   the ICU" (9.3.13 p183) - both stay FATAL while the transfer engine is unmodeled. */
constexpr Vr4122RegWindowModel kModel = {
    /*base=*/0x0F000040u,
    /*size=*/0x20u,
    /*num_regs=*/13u,
    /*wmask=*/{
        0x0001u,   /* 0x40 DMARSTREG  bit0 DMARST (9.3.1)                 */
        0x0000u,   /* 0x42 DMAIDLEREG R-only (9.3.2)                      */
        0x0001u,   /* 0x44 DMASENREG  bit0 DMASEN (9.3.3)                 */
        0x000Fu,   /* 0x46 DMAMSKREG  IOR/COUT/CIN/FOUT enables (9.3.4)   */
        0x0008u,   /* 0x48 DMAREQREG  bit3 DRQIOR R/W, 2:0 R (9.3.5)      */
        0x0003u,   /* 0x4A TDREG      IORAM/FIR directions (9.3.6)        */
        0x000Fu,   /* 0x4C DMAABITREG DMAPRI(3:0) (9.3.7)                 */
        0x000Fu,   /* 0x4E CONTROLREG FIREX/DMABLKS/AUTOINIT (9.3.8)      */
        0xFFFCu,   /* 0x50 BASSCNTLREG DMABS(15:2) (9.3.9)                */
        0x0003u,   /* 0x52 BASSCNTHREG DMABS(17:16) (9.3.10)              */
        0xFFFCu,   /* 0x54 CURRENTCNTLREG DMARBS(15:2) (9.3.11)           */
        0x0003u,   /* 0x56 CURRENTCNTHREG DMARBS(17:16) (9.3.12)          */
        0x0001u,   /* 0x58 TCINTREG   bit0 TCINT (9.3.13)                 */
    },
    /*reset=*/{
        0, 0x0001u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    /*fatal_on_set=*/{
        0, 0, 0, 0,
        0x0008u,   /* 0x48 DRQIOR=1 starts an unmodeled DMA transfer      */
        0, 0, 0, 0, 0, 0, 0,
        0x0001u,   /* 0x58 TCINT=1 pends an unmodeled BCUINT              */
    },
};

class Vr4122Dcu : public Vr4122RegWindowBase<kModel> {
public:
    using Vr4122RegWindowBase::Vr4122RegWindowBase;
};

}  /* namespace */

REGISTER_SERVICE(Vr4122Dcu);
