#pragma once

#include <chrono>
#include <cstdint>

/* Freescale GPT register map + bitfield constants, shared by the i.MX31
   (MCIMX31RM Ch 34) and i.MX51 (MCIMX51RM Ch 36) concretes - the IP is
   register-identical across both (verified MCIMX51RM Table 36-2 + Fig 36-3 +
   Table 36-5). */
namespace cerf_freescale_gpt_detail {

/* MCIMX31RM §34.3.1 Table 34-3 / MCIMX51RM §36.3 Table 36-2. */
inline constexpr uint32_t kSize   = 0x00004000u;
inline constexpr uint32_t kRegEnd = 0x28u;

inline constexpr uint32_t kOffGptcr   = 0x00u;
inline constexpr uint32_t kOffGptpr   = 0x04u;
inline constexpr uint32_t kOffGptsr   = 0x08u;
inline constexpr uint32_t kOffGptir   = 0x0Cu;
inline constexpr uint32_t kOffGptocr1 = 0x10u;
inline constexpr uint32_t kOffGptocr2 = 0x14u;
inline constexpr uint32_t kOffGptocr3 = 0x18u;
inline constexpr uint32_t kOffGpticr1 = 0x1Cu;
inline constexpr uint32_t kOffGpticr2 = 0x20u;
inline constexpr uint32_t kOffGptcnt  = 0x24u;

/* MCIMX31RM §34.3.3.1 Table 34-6 / MCIMX51RM §36.3.2.1 Table 36-5 GPTCR bits. */
inline constexpr uint32_t kGptcrEn        = 1u << 0;
inline constexpr uint32_t kGptcrEnmod     = 1u << 1;
inline constexpr uint32_t kGptcrClksrcSh  = 6;
inline constexpr uint32_t kGptcrClksrcM   = 7u << kGptcrClksrcSh;
inline constexpr uint32_t kGptcrFrr       = 1u << 9;
inline constexpr uint32_t kGptcrSwr       = 1u << 15;
inline constexpr uint32_t kGptcrFo1       = 1u << 29;
inline constexpr uint32_t kGptcrFo2       = 1u << 30;
inline constexpr uint32_t kGptcrFo3       = 1u << 31;
inline constexpr uint32_t kGptcrSelfClear = kGptcrSwr | kGptcrFo1 | kGptcrFo2 | kGptcrFo3;
/* SWR preserves EN/ENMOD/DBGEN/WAITEN/DOZEN/STOPEN/CLKSRC (Table 36-5[15]). */
inline constexpr uint32_t kGptcrSwrPreserve = 0x000003FFu;

/* MCIMX31RM §34.3.3.3 Table 34-8 / MCIMX51RM §36.3.2.3 GPTSR - w1c bits. */
inline constexpr uint32_t kGptOf1 = 1u << 0;
inline constexpr uint32_t kGptRov = 1u << 5;
inline constexpr uint32_t kGptStatusMask = 0x3Fu;

/* GPTCR CLKSRC encoding (MCIMX51RM Table 36-5, p1051; MCIMX31RM §34.3.3.1).
   GPT's low-freq (32k) is 100b=4, unlike EPIT's 011b; 011 (external) and 101
   (crystal) are unwired and FATAL at GPTCR write. */
inline constexpr uint32_t kClksrcNone     = 0u;
inline constexpr uint32_t kClksrcIpgClk   = 1u;
inline constexpr uint32_t kClksrcHighfreq = 2u;
inline constexpr uint32_t kClksrcLowfreq  = 4u;

inline constexpr auto     kPollInterval       = std::chrono::microseconds(100);
inline constexpr uint32_t kNotifyForwardLimit = 10000u;

}  /* namespace cerf_freescale_gpt_detail */
