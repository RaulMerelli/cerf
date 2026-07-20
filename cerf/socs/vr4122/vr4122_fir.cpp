#include "../vr41xx/vr41xx_fir_impl.h"

namespace {

/* VR4122 FIR base 0x0F000840 (NetBSD hpcmips vripreg.h VR4122_FIR_ADDR); guest
   serial.dll reads IRSR1 at 0x0F000858 = base+0x18. */
class Vr4122Fir : public cerf_vr41xx_fir_detail::Vr41xxFirBase<SocFamily::VR4122, 0x0F000840u> {
public:
    using Vr41xxFirBase::Vr41xxFirBase;
};

}  /* namespace */

REGISTER_SERVICE(Vr4122Fir);
