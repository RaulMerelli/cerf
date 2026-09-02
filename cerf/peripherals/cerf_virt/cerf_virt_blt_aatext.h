#pragma once

#include "cerf_virt_blt_pixelops.h"

#include <cstdint>
#include <cmath>

namespace CerfVirt {

struct AATextContext {
    uint32_t fl[3];
    int      shR[3];
    int      shL[3];
    uint32_t uF[3];
    uint32_t aulB[16];
    uint32_t aulIB[16];

    void Build(const uint32_t masks[3], uint32_t on_color, float gamma = 2.330f) {
        for (int c = 0; c < 3; ++c) {
            fl[c] = masks[c];
            int r = (int)BltPixelOps::HighBitPos(masks[c]) - 8;
            int l = 0;
            if (r < 0) { l = -r; r = 0; }
            shR[c] = r;
            shL[c] = l;
            uF[c] = ((on_color & masks[c]) >> r) << l;
        }
        for (int k = 0; k < 16; ++k) {
            const float a = (k > 0) ? (float)(k + 1) : 0.0f;
            aulB[k]  = (uint32_t)(65536.0f * std::pow(a / 16.0f, 1.0f / gamma));
            aulIB[k] = (uint32_t)(65536.0f - 65536.0f * std::pow(1.0f - a / 16.0f, 1.0f / gamma));
        }
    }

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

    uint32_t BlendClearType(uint32_t dst, uint32_t mask_index) const {
        /* hmi_ktp700_mobile_v17 ddraw_ipu.dll sub_EF52E490 indexes this table
           at unk_EF5483A8 for
           gpe8Bpp AAF0 masks.  Entries are sixths of R/G/B coverage. */
        static constexpr uint8_t kCoverage[115][3] = {
            {0, 0, 0}, {0, 0, 1}, {0, 0, 2}, {0, 1, 1}, {0, 1, 2}, {0, 1, 3}, {0, 2, 2}, {0, 2, 3}, {0, 2, 4},
            {1, 0, 0}, {1, 0, 1}, {1, 0, 2}, {1, 1, 0}, {1, 1, 1}, {1, 1, 2}, {1, 1, 3}, {1, 2, 1}, {1, 2, 2},
            {1, 2, 3}, {1, 2, 4}, {1, 3, 2}, {1, 3, 3}, {1, 3, 4}, {1, 3, 5}, {2, 0, 0}, {2, 0, 1}, {2, 0, 2},
            {2, 1, 0}, {2, 1, 1}, {2, 1, 2}, {2, 1, 3}, {2, 2, 0}, {2, 2, 1}, {2, 2, 2}, {2, 2, 3}, {2, 2, 4},
            {2, 3, 1}, {2, 3, 2}, {2, 3, 3}, {2, 3, 4}, {2, 3, 5}, {2, 4, 2}, {2, 4, 3}, {2, 4, 4}, {2, 4, 5},
            {2, 4, 6}, {3, 1, 0}, {3, 1, 1}, {3, 1, 2}, {3, 1, 3}, {3, 2, 0}, {3, 2, 1}, {3, 2, 2}, {3, 2, 3},
            {3, 2, 4}, {3, 3, 1}, {3, 3, 2}, {3, 3, 3}, {3, 3, 4}, {3, 3, 5}, {3, 4, 2}, {3, 4, 3}, {3, 4, 4},
            {3, 4, 5}, {3, 4, 6}, {3, 5, 3}, {3, 5, 4}, {3, 5, 5}, {3, 5, 6}, {4, 2, 0}, {4, 2, 1}, {4, 2, 2},
            {4, 2, 3}, {4, 2, 4}, {4, 3, 1}, {4, 3, 2}, {4, 3, 3}, {4, 3, 4}, {4, 3, 5}, {4, 4, 2}, {4, 4, 3},
            {4, 4, 4}, {4, 4, 5}, {4, 4, 6}, {4, 5, 3}, {4, 5, 4}, {4, 5, 5}, {4, 5, 6}, {4, 6, 4}, {4, 6, 5},
            {4, 6, 6}, {5, 3, 1}, {5, 3, 2}, {5, 3, 3}, {5, 3, 4}, {5, 4, 2}, {5, 4, 3}, {5, 4, 4}, {5, 4, 5},
            {5, 5, 3}, {5, 5, 4}, {5, 5, 5}, {5, 5, 6}, {5, 6, 4}, {5, 6, 5}, {5, 6, 6}, {6, 4, 2}, {6, 4, 3},
            {6, 4, 4}, {6, 5, 3}, {6, 5, 4}, {6, 5, 5}, {6, 6, 4}, {6, 6, 5}, {6, 6, 6},
        };
        static constexpr int32_t kBlend[7] = {0x000000, 0x02AAAB, 0x055555, 0x080000, 0x0AAAAB, 0x0D5555, 0x100000};
        /* hmi_ktp700_mobile_v17 ddraw_ipu.dll .data 0xEF5473A8 and 0xEF5474A8:
           255*(k/255)^1.5 and 255*(k/255)^(2/3).  sub_EF52E2A0 banks thirteen
           such pairs from 0xEF546AA8 at a 0x200 stride, selected on
           dword_EF5485B0 by twelve thresholds; this pair is the 0x5DC..0x640
           band. */
        struct Gamma15Tables {
            uint8_t to_linear[256];
            uint8_t from_linear[256];
            Gamma15Tables() {
                for (int k = 0; k < 256; ++k) {
                    const double a = (double)k / 255.0;
                    to_linear[k] = (uint8_t)(255.0 * std::pow(a, 1.5) + 0.5);
                    from_linear[k] = (uint8_t)(255.0 * std::pow(a, 2.0 / 3.0) + 0.5);
                }
            }
        };
        static const Gamma15Tables gamma;

        uint32_t u = 0;
        for (int c = 0; c < 3; ++c) {
            const uint32_t uT = ((dst & fl[c]) << shL[c]) >> shR[c];
            const int32_t bg = gamma.to_linear[uT];
            const int32_t fg = gamma.to_linear[uF[c]];
            const int32_t value = bg + (((fg - bg) * kBlend[kCoverage[mask_index][c]] + 0x80000) >> 20);
            const uint32_t out = gamma.from_linear[value];
            u |= (((out << shR[c]) >> shL[c]) & fl[c]);
        }
        return u;
    }
};

} // namespace CerfVirt
