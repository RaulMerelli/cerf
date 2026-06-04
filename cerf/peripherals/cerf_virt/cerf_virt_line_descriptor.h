#pragma once

#include "cerf_virt_blt_descriptor.h"   /* CerfBltSurface + the <1600 int typedefs */

/* One GPE line segment; the host replays GPE::EmulatedLine
   (WINCE600 .../DISPLAY/GPE/swline.cpp) by PA. */

#if defined(__cplusplus)
namespace CerfVirt {
#endif

/* Host validates this on read; mismatch = corrupt VA, halt rather than draw. */
const uint32_t kCerfLineMagic = 0x434C494Eu; /* 'CLIN' */

typedef struct CerfLineDescriptor {
    uint32_t magic;
    int32_t  x_start;       /* GPELineParms.xStart */
    int32_t  y_start;       /* GPELineParms.yStart */
    int32_t  c_pels;        /* GPELineParms.cPels  */
    uint32_t d_m;           /* GPELineParms.dM     */
    uint32_t d_n;           /* GPELineParms.dN     */
    int32_t  ll_gamma;      /* GPELineParms.llGamma */
    int32_t  i_dir;         /* octant 0..7         */
    uint32_t style;         /* dash bitmask        */
    int32_t  style_state;   /* GPELineParms.styleState */
    uint32_t solid_color;   /* pen                 */
    uint32_t mix;           /* low byte = mark ROP2, next byte = space ROP2 */
    CerfBltSurface dst;
} CerfLineDescriptor;

#if defined(__cplusplus)
}  /* namespace CerfVirt */
#endif
