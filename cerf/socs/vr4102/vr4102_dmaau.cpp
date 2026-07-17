#include "../vr41xx/vr41xx_dmaau_impl.h"

#include <cstdint>

namespace {

using cerf_vr41xx_dmaau_detail::Vr41xxDmaauBase;
using cerf_vr41xx_dmaau_detail::Vr41xxDmaauModel;

/* DMAAU registers 0x0B000020-0x0B000037 (UM Table 11-1). */
constexpr Vr41xxDmaauModel kModel = {
    /*base=*/0x0B000020u,
    /*size=*/0x20u,
    /* High registers writable D[8:0]; D[15:9] "Write 0 when writing" (UM 11.2.1). */
    /*high_writable_mask=*/0x01FFu,
};

class Vr4102Dmaau : public Vr41xxDmaauBase<SocFamily::VR4102, kModel> {
public:
    using Vr41xxDmaauBase::Vr41xxDmaauBase;
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Dmaau);
