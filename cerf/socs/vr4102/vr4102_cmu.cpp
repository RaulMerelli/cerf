#include "../vr41xx/vr41xx_reg_window_impl.h"

#include <cstdint>

namespace {

using cerf_vr41xx_reg_window_detail::ReadKind;
using cerf_vr41xx_reg_window_detail::Vr41xxRegWindowBase;
using cerf_vr41xx_reg_window_detail::Vr41xxRegWindowModel;
using cerf_vr41xx_reg_window_detail::WriteKind;

/* VR4102 UM Table 5-10 p139: CMU decodes 0x0B000060-0x0B00007F.
   VR4102 CMUCLKMSK, 0x0B000060 (VR4102 UM Table 13-1). D10 MSKFFIR, D9 MSKSHSP,
   D8 MSKSSIU, D5 MSKDSIU, D4 MSKFIR, D3 MSKKIU, D2 MSKAIU, D1 MSKSIU, D0 MSKPIU
   are R/W; D15:11 and D7:6 reserved; both reset rows 0 (VR4102 UM 13.2.1). */
constexpr Vr41xxRegWindowModel kModel = {
    /*base=*/0x0B000060u,
    /*size=*/0x20u,
    /*num_regs=*/1u,
    /*word_pairs=*/false,
    {
        { ReadKind::kStored, WriteKind::kStored, 0x073Fu, 0x0000u },
    },
};

class Vr4102Cmu : public Vr41xxRegWindowBase<SocFamily::VR4102, kModel> {
public:
    using Vr41xxRegWindowBase::Vr41xxRegWindowBase;
};

}

REGISTER_SERVICE(Vr4102Cmu);
