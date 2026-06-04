#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int uint32_t;
#else
#include <cstdint>
#endif

namespace CerfVirt {

const uint32_t kPtrX           = 0x00u;  /* 0..65535 normalized */
const uint32_t kPtrY           = 0x04u;  /* 0..65535 normalized */
const uint32_t kPtrButtons     = 0x08u;  /* bit0 L, bit1 R, bit2 M */
const uint32_t kPtrWheelAccum  = 0x0Cu;  /* signed, accumulates WHEEL_DELTA notches */
const uint32_t kPtrSeq         = 0x10u;  /* host bumps on any X/Y/Buttons/Wheel change */

const uint32_t kPtrButtonLeft   = 0x1u;
const uint32_t kPtrButtonRight  = 0x2u;
const uint32_t kPtrButtonMiddle = 0x4u;

}  /* namespace CerfVirt */
