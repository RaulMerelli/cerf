#pragma once

#include "../core/service.h"

#include <cstdint>

struct ParsedRom;

/* RomPlacer — copies each parsed ROM partition's flat bytes into
   emulated DRAM at the PA the SoC's OAT maps each partition's
   flat_base_va to. */
class RomPlacer : public Service {
public:
    using Service::Service;
    void OnReady() override;

private:
    /* Copies one partition's XIP regions to their OAT-mapped PAs.
       volatile_only=true (hard-reset replay) re-places only copies whose
       destination is volatile RAM; flash-resident copies survive the
       power cycle and re-copying them would clobber guest flash writes. */
    void PlaceRomXips(const ParsedRom& rom, bool volatile_only);
    bool IsVolatilePa(uint32_t pa);
};
