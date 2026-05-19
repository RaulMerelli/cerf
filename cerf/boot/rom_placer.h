#pragma once

#include "../core/service.h"

/* RomPlacer — copies each parsed ROM partition's flat bytes into
   emulated DRAM at the PA the SoC's OAT maps each partition's
   flat_base_va to. */
class RomPlacer : public Service {
public:
    using Service::Service;
    void OnReady() override;
};
