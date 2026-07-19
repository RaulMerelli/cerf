#include "../vr41xx/vr41xx_cmu_impl.h"

namespace {

using cerf_vr41xx_cmu_detail::Vr41xxCmuModel;

/* VR4122 CMUCLKMSK, 0x0F000060 (NetBSD vripreg.h VR4122_CMU_ADDR): R/W bits
   13,12,11,10,8,7,6,4,1 (VR4131 UM 10.2.1; NetBSD cmureg.h VR4122_CMUMSK*). */
constexpr Vr41xxCmuModel kModel = { 0x0F000060u, 0x20u, 0x3DD2u };

class Vr4122Cmu : public cerf_vr41xx_cmu_detail::Vr41xxCmuBase<SocFamily::VR4122, kModel> {
public:
    using Vr41xxCmuBase::Vr41xxCmuBase;
};

}  /* namespace */

REGISTER_SERVICE(Vr4122Cmu);
