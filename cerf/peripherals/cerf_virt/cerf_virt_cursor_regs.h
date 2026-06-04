#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int   uint32_t;
typedef unsigned char  uint8_t;
#else
#include <cstdint>
#endif

namespace CerfVirt {

/* cursor channel: the guest's GPE::SetPointerShape fills a CerfCursorDescriptor
   (1bpp AND/XOR masks), writes its VA to kCurDescVa, then kicks; the host
   reads it through the live MMU and builds the host HCURSOR. */
const uint32_t kCurDescVa = 0x000u;
const uint32_t kCurKick   = 0x004u;

const uint32_t kCursorMaxDim    = 64u;
const uint32_t kCursorMaxStride = (kCursorMaxDim + 7u) / 8u;
const uint32_t kCursorBitsBytes = kCursorMaxStride * kCursorMaxDim * 2u;

struct CerfCursorDescriptor {
    uint32_t visible;   /* 1 = shape present, 0 = guest hid the cursor */
    uint32_t cx;
    uint32_t cy;
    uint32_t xhot;
    uint32_t yhot;
    uint32_t stride;    /* bytes per row of the mask planes below */
    uint8_t  bits[kCursorBitsBytes];  /* AND rows [0,cy), XOR rows [cy,2cy), MSB-first */
};

}  /* namespace CerfVirt */
