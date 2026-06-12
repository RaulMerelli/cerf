#pragma once

#include <chrono>
#include <cstdint>

/* iMX31 GPT register map + bitfield constants (MCIMX31RM Ch 34). */
namespace cerf_imx31_gpt_regs {

/* MCIMX31RM §34.3.1 Table 34-3 (PDF p1474). */
inline constexpr uint32_t kBase   = 0x53F90000u;
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

/* MCIMX31RM §34.3.3.1 Table 34-6 GPTCR bits (PDF p1477..1480). */
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
/* SWR preserves EN/ENMOD/DBGEN/WAITEN/DOZEN/STOPEN/CLKSRC (§34.4.1[15]). */
inline constexpr uint32_t kGptcrSwrPreserve = 0x000003FFu;

/* MCIMX31RM §34.3.3.3 Table 34-8 GPTSR (PDF p1481) — w1c bits. */
inline constexpr uint32_t kGptOf1 = 1u << 0;
inline constexpr uint32_t kGptOf2 = 1u << 1;
inline constexpr uint32_t kGptOf3 = 1u << 2;
inline constexpr uint32_t kGptIf1 = 1u << 3;
inline constexpr uint32_t kGptIf2 = 1u << 4;
inline constexpr uint32_t kGptRov = 1u << 5;
inline constexpr uint32_t kGptStatusMask = 0x3Fu;

inline constexpr uint32_t kClksrcNone   = 0u;
inline constexpr uint32_t kClksrcIpgClk = 1u;

/* MCIMX31RM §2.2 Table 2-3 (PDF p190): AVIC source 29 = GPT. */
inline constexpr uint32_t kAvicSourceGpt = 29u;

inline constexpr auto     kPollInterval       = std::chrono::microseconds(100);
inline constexpr uint32_t kNotifyForwardLimit = 10000u;

}  /* namespace cerf_imx31_gpt_regs */
