#pragma once

#include <cstdint>
#include <numeric>

/* OMAP3530 TRM SPRUF98Y §16.2.4 (printed p. 2605): a GP timer is clocked from
   the 32-kHz clock or the system clock. §16.2.4.2.1 (printed p. 2610) gives
   that clock as 32,768 Hz. §16.6.1 (printed p. 2660): the sync counter is
   clocked by the 32-kHz system clock. */
constexpr uint64_t kOmap3530Clk32kHz = 32768ull;

constexpr uint64_t kOmap3530NsPerSec  = 1000000000ull;
constexpr uint64_t kOmap3530ScaleGcd  =
    std::gcd(kOmap3530NsPerSec, kOmap3530Clk32kHz);
constexpr uint64_t kOmap3530NsPerUnit = kOmap3530NsPerSec / kOmap3530ScaleGcd;
constexpr uint64_t kOmap3530TkPerUnit = kOmap3530Clk32kHz / kOmap3530ScaleGcd;
