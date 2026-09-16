#pragma once

#include "cerf_virt_blt_pixelops.h"

#include <algorithm>
#include <array>
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
    std::array<std::array<uint8_t, 3>, 115> clear_type_coverage{};

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
        std::size_t index = 0u;
        for (int r = 0; r <= 6; ++r) {
            const int first_g = r > 2 ? r - 2 : 0;
            const int last_g = r < 4 ? r + 2 : 6;
            for (int g = first_g; g <= last_g; ++g) {
                const int first_b = std::max(0, std::max(g - 2, g - r));
                const int last_b = std::min(6, std::min(g + 2, g + 6 - r));
                for (int b = first_b; b <= last_b; ++b) {
                    clear_type_coverage[index++] = {
                        static_cast<uint8_t>(r),
                        static_cast<uint8_t>(g),
                        static_cast<uint8_t>(b),
                    };
                }
            }
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
        /* hmi_ktp700_mobile_v17 ddraw_ipu.dll sub_EF52E490: gpe8Bpp AAF0
           mask indices map to sixths of R/G/B coverage. */
        const auto& coverage = clear_type_coverage[mask_index];
        static constexpr int32_t kBlend[7] = {0x000000, 0x02AAAB, 0x055555, 0x080000, 0x0AAAAB, 0x0D5555, 0x100000};
        /* hmi_ktp700_mobile_v17 ddraw_ipu.dll @0xEF5473A8/@0xEF5474A8 contains
           the paired 255*(k/255)^1.5 and 255*(k/255)^(2/3) lookup tables. */
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
            const int32_t value = bg + (((fg - bg) * kBlend[coverage[c]] + 0x80000) >> 20);
            const uint32_t out = gamma.from_linear[value];
            u |= (((out << shR[c]) >> shL[c]) & fl[c]);
        }
        return u;
    }
};

} // namespace CerfVirt
