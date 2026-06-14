#pragma once

#include "../../core/service.h"

#include <cstdint>
#include <optional>
#include <vector>

/* The `.sec` packs partitions by size, but the device NAND holds them
   block-aligned at cumulative 512 KB StartBlocks — a linear `.sec` map feeds the
   stub's block-1 scan the wrong bytes. Placement = the flasher's own algorithm
   (staged_80040000.bin sub_800593c0 + DPS_Write 0x800582FC), verified vs `.sec`. */
class Imx51NandLayout : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    /* Physical NAND byte offset -> `.sec` flash byte offset. nullopt = a blank
       (erased, 0xFF) region: past a module's data, or an unmapped block. */
    std::optional<uint64_t> PhysToSec(uint64_t phys_off) const;

private:
    struct Part {
        uint32_t id;
        uint64_t start_block;
        uint64_t nblocks;
        uint64_t sec_off;
        uint64_t size;          /* bytes of real data in the module (rest of its
                                   blocks read blank) */
    };

    std::vector<Part> parts_;

    /* 512 KB block (flash-0x0 config header +0x58; MCIMX51RM Table 9-3, S10). */
    static constexpr uint64_t kBlock = 0x80000u;
    /* config + first-stage IPL region; the first XIP module (SBOOT) starts at
       `.sec` 0x5000 (image base 0x4000, flash_header @0x4400, S5). */
    static constexpr uint64_t kBootRegion = 0x5000u;
};
