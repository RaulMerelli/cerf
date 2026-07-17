#include "../vr41xx/vr41xx_dmaau_impl.h"

#include <cstdint>

namespace {

using cerf_vr41xx_dmaau_detail::Vr41xxDmaauBase;
using cerf_vr41xx_dmaau_detail::Vr41xxDmaauModel;

/* DMAAU registers 0x0B000020-0x0B000037 (UM Table 12-1). */
constexpr Vr41xxDmaauModel kModel = {
    /*base=*/0x0B000020u,
    /*size=*/0x20u,
    /* High registers writable D[10:0]; D[15:11] "Write 0 to these bits" (UM 12.2.1). */
    /*high_writable_mask=*/0x07FFu,
};

class Vr4121Dmaau : public Vr41xxDmaauBase<SocFamily::VR4121, kModel> {
public:
    using Vr41xxDmaauBase::Vr41xxDmaauBase;
};

}  /* namespace */

REGISTER_SERVICE(Vr4121Dmaau);
