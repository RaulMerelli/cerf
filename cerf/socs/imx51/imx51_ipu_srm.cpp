#include "imx51_ipu_internal_mem.h"

namespace {

/* CM Shadow / SRM (System Register Manager shadow) @ 0x5F040000, MCIMX51RM
   Table 42-1. The DP registers are not directly accessible and are programmed
   through this shadow (Table 42-1 DP note); ddraw_ipu.dll DisplayInit
   (sub_C0A5931C / sub_C0A59344) writes it during the i.MX51 display bring-up. */
class Imx51IpuSrm : public cerf_imx51_ipu_mem_detail::Imx51IpuInternalMem<0x5F040000u> {
    using Imx51IpuInternalMem::Imx51IpuInternalMem;
};

}  /* namespace */

REGISTER_SERVICE(Imx51IpuSrm);
