#pragma once

#include "cerf_virt_blt_pixelops.h"

#include <cstdint>
#include <cmath>

namespace CerfVirt {

/* Grayscale anti-aliased text (rop4==0xAAF0 + 4bpp coverage mask) gamma blend,
   mirroring WINCE600 AABLT aablt.cpp AAMASKINFO + AATextBltDst16/24/32. */
struct AATextContext {
    uint32_t fl[3];      /* R,G,B channel masks (aablt flRed/flGre/flBlu) */
    int      shR[3];     /* iRedR/iGreR/iBluR (channel right shift) */
    int      shL[3];     /* iRedL/iGreL/iBluL (channel left shift) */
    uint32_t uF[3];      /* foreground (text colour) channel value (SetUF) */
    uint32_t aulB[16];   /* gamma table */
    uint32_t aulIB[16];  /* inverse-gamma table */

    /* gamma default 2.330 == aablt.cpp c_ulGammaDefault(2330)/1000; the shift
       derivation + table formulas are GetAAMaskInfo + SetGamma verbatim. */
    void Build(const uint32_t masks[3], uint32_t on_color, float gamma = 2.330f) {
        for (int c = 0; c < 3; ++c) {
            fl[c] = masks[c];
            int r = (int)BltPixelOps::HighBitPos(masks[c]) - 8;  /* topbit - 8 */
            int l = 0;
            if (r < 0) { l = -r; r = 0; }
            shR[c] = r;
            shL[c] = l;
            uF[c] = ((on_color & masks[c]) >> r) << l;           /* SetUF */
        }
        for (int k = 0; k < 16; ++k) {
            const float a = (k > 0) ? (float)(k + 1) : 0.0f;
            aulB[k]  = (uint32_t)(65536.0f * std::pow(a / 16.0f, 1.0f / gamma));
            aulIB[k] = (uint32_t)(65536.0f - 65536.0f * std::pow(1.0f - a / 16.0f, 1.0f / gamma));
        }
    }

    /* cov = 4-bit coverage 1..14 (caller handles 0 and 15). The unsigned wrap of
       (uF-uT) feeding both the (int32_t)dT<0 table select and the product is
       load-bearing: signed arithmetic here selects the wrong gamma table and
       shifts the blend (BlendPixelAA). */
    uint32_t BlendAA(uint32_t dst, uint32_t cov) const {
        uint32_t u = 0;
        for (int c = 0; c < 3; ++c) {
            const uint32_t uT = ((dst & fl[c]) << shL[c]) >> shR[c];
            const uint32_t dT = uF[c] - uT;
            const uint32_t* tab = ((int32_t)dT < 0) ? aulIB : aulB;
            u |= ((((dT * tab[cov] + (uT << 16)) >> 16) << shR[c]) >> shL[c]) & fl[c];
        }
        return u;
    }
};

}  /* namespace CerfVirt */
