#include "../vr41xx/vr41xx_cmu_impl.h"

namespace {

using cerf_vr41xx_cmu_detail::Vr41xxCmuModel;

/* VR4102 CMUCLKMSK, 0x0B000060 (VR4102 UM Table 13-1). D10 MSKFFIR, D9 MSKSHSP,
   D8 MSKSSIU, D5 MSKDSIU, D4 MSKFIR, D3 MSKKIU, D2 MSKAIU, D1 MSKSIU, D0 MSKPIU
   are R/W; D15:11 and D7:6 reserved (VR4102 UM 13.2.1). */
constexpr Vr41xxCmuModel kModel = { 0x0B000060u, 0x20u, 0x073Fu };

class Vr4102Cmu : public cerf_vr41xx_cmu_detail::Vr41xxCmuBase<SocFamily::VR4102, kModel> {
public:
    using Vr41xxCmuBase::Vr41xxCmuBase;
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Cmu);
