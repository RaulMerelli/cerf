#pragma once

#include <cstdint>

namespace imx6_gic_detail {

/* Cortex-A9 MPCore TRM DDI 0407I Table 3-6: ICCIDR reset value. */
inline constexpr uint32_t kIccIdr = 0x3901243Bu;

/* Cortex-A9 MPCore TRM DDI 0407I Table 3-1: ICDIIDR reset value. */
inline constexpr uint32_t kIcdIidr = 0x0102043Bu;

/* Cortex-A9 MPCore TRM DDI 0407I Table 3-4: ICDICTR carries LSPI[15:11] = 31,
   SecurityExtn[10] = 1, CPU number[7:5] = 0 for one processor, and IT lines
   number[4:0] = 0b00100 for "160 interrupts, 128 external interrupt lines",
   which is the count IMX6DQRM Rev.2 §3.2 gives the GIC. */
inline constexpr uint32_t kIcdIctr = 0x0000FC04u;

}
