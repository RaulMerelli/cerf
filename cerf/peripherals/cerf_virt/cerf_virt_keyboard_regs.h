#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int uint32_t;
#else
#include <cstdint>
#endif

namespace CerfVirt {

/* Host->guest keyboard edge-event ring (key down/up delivered once, in order). */
const uint32_t kKbWriteSeq  = 0x00u;  /* host: monotonic count of events written */
const uint32_t kKbRingBase  = 0x10u;  /* first ring entry word */
const uint32_t kKbRingCount = 256u;   /* ring capacity in entries */

/* Ring entry word: bits[7:0] = VK, bit[8] = key_up (1 = release). */
const uint32_t kKbEntryVkMask   = 0x00FFu;
const uint32_t kKbEntryKeyUpBit = 0x0100u;

}  /* namespace CerfVirt */
