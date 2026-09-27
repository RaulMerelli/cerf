#pragma once

#include <cstdint>

namespace imx6_gic_detail {

/* Cortex-A9 MPCore TRM DDI 0407F Table 3-8: ICCIDR reset value. */
inline constexpr uint32_t kIccIdr = 0x3901243Bu;

/* Cortex-A9 MPCore TRM DDI 0407F Table 3-1: ICDIIDR reset value. */
inline constexpr uint32_t kIcdIidr = 0x0102043Bu;

/* DDI 0407F Table 3-4: LSPI[15:11] b11111 = 31, SecurityExtn[10] = 1, CPU
   number[7:5] = 0, IT lines[4:0] b00100 = "160 interrupts, 128 external
   interrupt lines"; IMX6DQRM Rev.2 §3.2 gives the GIC 128 requests. */
inline constexpr uint32_t kIcdIctr = 0x0000FC04u;

}
