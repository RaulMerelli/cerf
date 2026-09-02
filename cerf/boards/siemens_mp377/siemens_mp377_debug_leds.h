#pragma once

#include <cstdint>

namespace siemens_mp377 {

/* P377 OAL debug LED/display registers.
   siemens_mp377_v1040 nk.exe sub_804414A8 maps these physical addresses and sub_80441638 is the
   observed writer behind the WinCE OALLED/OEMWriteDebugLED path.  The device
   is attached to the IOP13xx PBI/external-bus window, so board_io keeps loud
   guards around these exact apertures. */
inline constexpr uint32_t kDebugLedProgressBase = 0xF2FFFFF6u;
inline constexpr uint32_t kDebugLedProgressEnd = 0xF2FFFFFEu;
inline constexpr uint32_t kDebugLedTickBase = 0xF3400020u;
inline constexpr uint32_t kDebugLedTickEnd = 0xF3400022u;

} // namespace siemens_mp377
