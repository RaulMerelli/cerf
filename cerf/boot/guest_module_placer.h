#pragma once

#include "../core/service.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

/* One placement unit (PE section, or an IMGFS packed slot — both carry the
   same geometry). */
struct ModuleSection {
    uint32_t rva;
    uint32_t vsize;
    uint32_t psize;
    uint32_t flags;
};

/* Decides the runtime VA base for an injected guest-additions XIP module:
   the victim's own vbase when the image fits its fixed slot, or a relocated
   section-1 vbase the kernel will reserve when the image overflows it. */
class GuestModulePlacer : public Service {
public:
    using Service::Service;

    uint32_t ComputeVbase(uint32_t orig_vbase, uint32_t orig_slot_base,
                          uint32_t image_size, uint32_t ce_major,
                          uint32_t e32_off_vbase, const char* victim_name);

    /* The kernel reserves each XIP's [ROMHDR.dllfirst<<16, dlllast) per process
       (loader.c ReserveDllRW); lowering the governing XIP's dllfirst in DRAM is
       what enlarges that reservation to cover the injected writable sections. */
    bool ExtendDllRwRegion(const std::vector<ModuleSection>& units,
                           uint32_t base_vbase, uint32_t slot_base,
                           uint32_t& out_data_base);

    /* When data_base != 0, writable non-shared units are laid out from
       data_base — a per-process slot-0 base, so the device.exe carrier loading
       the same module gets its own copy and cannot clobber gwes's writable
       data; other units stay at base_vbase + slot_base + rva. */
    std::vector<uint32_t> ComputeSectionRealaddrs(
        const std::vector<ModuleSection>& units,
        uint32_t base_vbase, uint32_t slot_base, uint32_t data_base);

private:
    /* Governing-XIP ROMHDR PA -> its current DLL-RW floor; a second victim in
       the same XIP must drop below the floor the first one already set. */
    std::unordered_map<uint32_t, uint32_t> extended_floors_;
};
