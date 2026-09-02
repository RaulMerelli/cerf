#pragma once

#include <cstdint>

#include "arm_mmu_state.h"

/* ARM DDI 0406C.d Tables B3-10 through B3-13 (pp. B3-1363..B3-1366)
   define the short-descriptor memory type, cacheability, and shareability.
   B4.1.112 (pp. B4-1659..B4-1660) defines their positions in PAR[10:1]. */
inline uint16_t ArmShortDescriptorParAttributes(uint32_t tex, bool c, bool b, bool s, bool ns, bool supersection,
                                                bool tex_remap, uint32_t prrr, uint32_t nmrr) {
    constexpr uint16_t kParSs = 1u << 1;
    constexpr uint16_t kParNs = 1u << 9;
    constexpr uint16_t kParSh = 1u << 7;
    constexpr uint16_t kParNos = 1u << 10;
    constexpr uint16_t kInnerStronglyOrdered = 1u << 4;
    constexpr uint16_t kInnerDevice = 3u << 4;
    static constexpr uint16_t kInnerCache[4] = {0u << 4, 5u << 4, 6u << 4, 7u << 4};

    uint16_t attrs = (ns ? kParNs : 0u) | (supersection ? kParSs : 0u);
    bool shareable = false;
    bool normal = false;
    uint32_t inner = 0u;
    uint32_t outer = 0u;
    uint32_t region = 0u;

    if (tex_remap) {
        /* DDI 0406C.d Table B3-12: TEX[0]:C:B selects PRRR.TRn and,
           for Normal memory, the corresponding NMRR cache fields. */
        region = ((tex & 1u) << 2) | (static_cast<uint32_t>(c) << 1) | static_cast<uint32_t>(b);
        const uint32_t type = (prrr >> (region * 2u)) & 3u;
        if (type == 0u) {
            attrs |= kInnerStronglyOrdered;
            shareable = true;
        } else if (type == 1u) {
            attrs |= kInnerDevice;
            shareable = ((prrr >> (16u + static_cast<uint32_t>(s))) & 1u) != 0u;
        } else {
            /* PRRR.TRn == 2 is Normal. TRn == 3 is UNPREDICTABLE; keeping
               both cache fields zero exposes no invented cacheability. */
            normal = true;
            if (type == 2u) {
                inner = (nmrr >> (region * 2u)) & 3u;
                outer = (nmrr >> (16u + region * 2u)) & 3u;
                shareable = ((prrr >> (18u + static_cast<uint32_t>(s))) & 1u) != 0u;
            }
        }
    } else if (tex == 0u) {
        if (!c && !b) {
            attrs |= kInnerStronglyOrdered;
            shareable = true;
        } else if (!c && b) {
            attrs |= kInnerDevice;
            shareable = true;
        } else {
            normal = true;
            inner = (static_cast<uint32_t>(c) << 1) | static_cast<uint32_t>(b);
            outer = inner;
            shareable = s;
        }
    } else if ((tex & 4u) != 0u) {
        /* Table B3-10 encoding 1BB: C:B is Inner, TEX[1:0] is Outer. */
        normal = true;
        inner = (static_cast<uint32_t>(c) << 1) | static_cast<uint32_t>(b);
        outer = tex & 3u;
        shareable = s;
    } else if (tex == 1u) {
        /* The two architecturally defined TEX=001 encodings are Normal
           Non-cacheable (C:B=00) and WB/WA (C:B=11). Reserved and
           IMPLEMENTATION DEFINED encodings expose no cacheability. */
        normal = true;
        if (c && b) inner = outer = 1u;
        shareable = s;
    } else if (tex == 2u && !c && !b) {
        attrs |= kInnerDevice;
    }

    if (normal) {
        attrs |= kInnerCache[inner] | static_cast<uint16_t>(outer << 2);
    }
    if (shareable) {
        attrs |= kParSh;
        /* Without TEX remap, S does not distinguish Inner from Outer
           Shareable. CERF reports Outer Shareable. With remap, PRRR.NOSn
           makes that distinction (DDI 0406C.d B3-1367/B4-1693). */
        if (tex_remap && ((prrr >> (24u + region)) & 1u) != 0u) attrs |= kParNos;
    }
    return attrs;
}

inline uint16_t ArmMmuDisabledDataParAttributes() {
    /* DDI 0406C.d B3.2.1 (p. B3-1312): with stage-1 disabled, a data
       translation is Strongly-Ordered and therefore Shareable. */
    return static_cast<uint16_t>((1u << 4) | (1u << 7));
}

/* DDI 0406C.d Figure B3-4 (p. B3-1325) short-descriptor field positions:
   section TEX[14:12] C[3] B[2] S[16] NS[19] SS[18]; large page TEX[14:12]
   C[3] B[2] S[10]; small page TEX[8:6] C[3] B[2] S[10]; the coarse-table
   L1 entry carries NS[3] for both page sizes. */
inline uint16_t ArmSectionParAttributes(uint32_t l1, bool tex_remap, uint32_t prrr, uint32_t nmrr) {
    return ArmShortDescriptorParAttributes((l1 >> 12) & 7u, ((l1 >> 3) & 1u) != 0u, ((l1 >> 2) & 1u) != 0u,
                                           ((l1 >> 16) & 1u) != 0u, ((l1 >> 19) & 1u) != 0u, ((l1 >> 18) & 1u) != 0u,
                                           tex_remap, prrr, nmrr);
}

inline uint16_t ArmLargePageParAttributes(uint32_t l2, uint32_t l1, bool tex_remap, uint32_t prrr, uint32_t nmrr) {
    return ArmShortDescriptorParAttributes((l2 >> 12) & 7u, ((l2 >> 3) & 1u) != 0u, ((l2 >> 2) & 1u) != 0u,
                                           ((l2 >> 10) & 1u) != 0u, ((l1 >> 3) & 1u) != 0u, false, tex_remap, prrr,
                                           nmrr);
}

inline uint16_t ArmSmallPageParAttributes(uint32_t l2, uint32_t l1, bool tex_remap, uint32_t prrr, uint32_t nmrr) {
    return ArmShortDescriptorParAttributes((l2 >> 6) & 7u, ((l2 >> 3) & 1u) != 0u, ((l2 >> 2) & 1u) != 0u,
                                           ((l2 >> 10) & 1u) != 0u, ((l1 >> 3) & 1u) != 0u, false, tex_remap, prrr,
                                           nmrr);
}

/* SCTLR.TRE plus PRRR/NMRR are the remap inputs every descriptor decode
   needs (ARM DDI 0406C.d B3.5.3); taking them from the MMU state keeps the
   walk's call sites to the descriptor words it already holds. */
inline uint16_t ArmSectionParAttributes(const ArmMmuState& st, uint32_t l1, bool modern_v6_fmt) {
    if (!modern_v6_fmt) return 0u;
    return ArmSectionParAttributes(l1, st.effective_control_register.bits.tre != 0u, st.prrr, st.nmrr);
}

inline uint16_t ArmSmallPageParAttributes(const ArmMmuState& st, uint32_t l2, uint32_t l1, bool modern_v6_fmt) {
    if (!modern_v6_fmt) return 0u;
    return ArmSmallPageParAttributes(l2, l1, st.effective_control_register.bits.tre != 0u, st.prrr, st.nmrr);
}

inline uint16_t ArmLargePageParAttributes(const ArmMmuState& st, uint32_t l2, uint32_t l1, bool modern_v6_fmt) {
    if (!modern_v6_fmt) return 0u;
    return ArmLargePageParAttributes(l2, l1, st.effective_control_register.bits.tre != 0u, st.prrr, st.nmrr);
}
