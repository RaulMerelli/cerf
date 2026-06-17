#include "imx51_ssi_impl.h"

namespace {

/* SSI3 @ 0x83FE8000 (MCIMX51RM Table 2-1; wavedev2_cs42448.dll BSPAudioInit
   sub_C0CB65B0 MmMapIoSpace 0x83FE8000, mapped only when the board reports
   >= 2 audio ports). */
class Imx51Ssi3 : public cerf_imx51_ssi_detail::Imx51SsiImpl<0x83FE8000u> {
    using Imx51SsiImpl::Imx51SsiImpl;
};

}  /* namespace */

REGISTER_SERVICE(Imx51Ssi3);
