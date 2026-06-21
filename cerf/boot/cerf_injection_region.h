#pragma once

#include "../core/service.h"

#include <cstdint>

/* Owns the guest-additions injection band: a CERF-backed PA region the MMU
   walker overlay serves at a guest-unmapped static-window VA, so the injected
   stub's e32/o32/section bytes live in CERF memory and the victim's TOC is
   only repointed at the band VA - never squatted into the victim's section. */
class CerfInjectionRegion : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    /* Band VA base; reserves the PA region + installs the overlay on first
       call. Lazy so a GA board that injects no victim never reserves it, and a
       board whose OAT leaves no static-window hole only halts when a victim
       actually needs the band. Halts if no hole exists. */
    uint32_t BandVaBase();
    uint32_t BandPaBase() const;
    uint32_t BandSize() const;

private:
    uint32_t va_base_ = 0;
};
