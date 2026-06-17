#include "imx51_ssi_impl.h"

namespace {

/* SSI1 @ 0x83FCC000 (MCIMX51RM Table 2-1; wavedev2_cs42448.dll BSPAudioInit
   sub_C0CB65B0 MmMapIoSpace 0x83FCC000). */
class Imx51Ssi1 : public cerf_imx51_ssi_detail::Imx51SsiImpl<0x83FCC000u> {
    using Imx51SsiImpl::Imx51SsiImpl;
};

}  /* namespace */

REGISTER_SERVICE(Imx51Ssi1);
