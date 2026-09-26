#pragma once

#include "../../core/log.h"
#include "imx6_vivante_state.h"

#include <cstdint>

namespace imx6_vivante {

struct VivanteIdentityProfile {
    uint32_t chip_identity;
    uint32_t model;
    uint32_t revision;
    uint32_t date;
    uint32_t time;
    uint32_t features;
    uint32_t minor[6];
    uint32_t specs[4];
};

inline constexpr VivanteIdentityProfile kGc320Identity = {
    0u,
    0x0320u,
    0x5007u,
    0x20120617u,
    0u,
    0xE02C7ECAu,
    {0xC1399EFFu, 0x020FB2DBu, 0u, 0u, 0u, 0u},
    {0xA4410A61u, 0x01000008u, 0x00000081u, 0u},
};

inline constexpr VivanteIdentityProfile kGc2000Identity = {
    0u,
    0x2000u,
    0x5108u,
    0x20120617u,
    0u,
    0xE0296CADu,
    {0xC9799EFFu, 0x2EFBF2D9u, 0u, 0u, 0u, 0u},
    {0x94410A68u, 0u, 0x000000C1u, 0u},
};

inline constexpr VivanteIdentityProfile kGc355Identity = {
    0u, 0x0355u, 0x1215u, 0x20120617u, 0u, 0x1E2C7CC8u, {0x0024207Fu, 0u, 0x00010104u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u},
};

static_assert((kGc320Identity.features & (1u << 9)) != 0u);
static_assert((kGc2000Identity.features & (1u << 9)) == 0u);
static_assert((kGc355Identity.features & (1u << 9)) == 0u);
/* etnaviv common.xml chipMinorFeatures1 bit 28 is MMU_VERSION. */
static_assert((kGc320Identity.minor[1] & (1u << 28)) == 0u);
static_assert((kGc2000Identity.minor[1] & (1u << 28)) == 0u);
static_assert((kGc355Identity.minor[1] & (1u << 28)) == 0u);

inline const VivanteIdentityProfile& IdentityFor(VivanteCore core) {
    switch (core) {
    case VivanteCore::Gc20003d: return kGc2000Identity;
    case VivanteCore::Gc3202d: return kGc320Identity;
    case VivanteCore::Gc355Vg: return kGc355Identity;
    }
    LOG(Caution, "imx6-vivante: no identity profile for core %u\n", static_cast<unsigned>(core));
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

}
