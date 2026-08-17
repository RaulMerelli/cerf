#pragma once

#include <cstdint>

namespace siemens_mp377 {

inline constexpr uint32_t kMp377PowerResetBase = 0xD0180000u;
inline constexpr uint32_t kMp377PowerResetBlockBytes = 0x1000u;
inline constexpr uint32_t kMp377PowerResetBlockCount = 3u;
inline constexpr uint32_t kMp377PowerResetEnd = kMp377PowerResetBase +
    kMp377PowerResetBlockBytes * kMp377PowerResetBlockCount;

/* P377 BSPIntrInit maps SYSINTR_POWERFAIL (23) to raw IOP13xx interrupt
   source 0x1F.  PowerFail.dll passes SYSINTR 23 to InterruptInitialize().
   These constants are descriptive in this passive model: D018 does not
   synthesize or assert source 0x1F by itself. */
inline constexpr uint32_t kMp377PowerFailSysIntr = 23u;
inline constexpr uint32_t kMp377PowerFailIrqSource = 0x1Fu;

}  // namespace siemens_mp377

