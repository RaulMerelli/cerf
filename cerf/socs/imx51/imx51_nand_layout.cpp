#include "imx51_nand_layout.h"

#include "../../boards/board_detector.h"
#include "../../boot/sec_flash.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"

#include <array>
#include <cstdint>

REGISTER_SERVICE(Imx51NandLayout);

namespace {

/* The flash-0x0 config block (`.sec` offset 0x0): magic word + per-module
   records from +0x60, stride 0x30 — the same {delim, id@+4, size@+0xC}
   structure SBOOT reads via its DPS lookup (Bootloader.bin 0x8FF0D63C),
   decoded from the device `.sec`. */
constexpr uint32_t kCfgMagic  = 0x400DB1B1u;
constexpr uint32_t kRecDelim  = 0xC001C000u;
constexpr uint32_t kRecBase   = 0x60u;
constexpr uint32_t kRecStride = 0x30u;

uint32_t Rd32(const uint8_t* p) {
    return  static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

}  /* namespace */

bool Imx51NandLayout::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    if (!bd || bd->GetSoc() != SocFamily::iMX51) return false;
    auto* sf = emu_.TryGet<SecFlash>();
    return sf && sf->IsPresent();
}

void Imx51NandLayout::OnReady() {
    auto& sf = emu_.Get<SecFlash>();

    std::array<uint8_t, kRecBase + kRecStride * 16> cfg{};
    sf.ReadFlash(0, cfg.data(), cfg.size());
    if (Rd32(cfg.data()) != kCfgMagic) {
        LOG(Caution, "Imx51NandLayout: flash-0x0 config magic mismatch (0x%08X)\n",
            Rd32(cfg.data()));
        return;
    }

    /* Walk the module records {delim, id@+4, size@+0xC}, assigning each a
       cumulative ceil(size/block) block span (manifest order). The `.sec` packs
       the modules by size: the config/IPL region is `.sec` 0x0..kBootRegion and
       every later module follows by size from kBootRegion. */
    uint64_t start_block = 0;
    uint64_t sec_off     = kBootRegion;
    for (uint32_t i = 0; i < 16; ++i) {
        const uint8_t* rec = cfg.data() + kRecBase + i * kRecStride;
        if (Rd32(rec) != kRecDelim) break;
        const uint32_t id   = Rd32(rec + 4);
        if (id == 0xFFu) break;
        const uint64_t size = Rd32(rec + 0xC);

        Part p{};
        p.id          = id;
        p.start_block = start_block;
        if (id == 0) {
            /* The config record covers the whole boot region (config + IPL stub),
               .sec 0x0..kBootRegion, so block 0 reads serve the stub bytes. */
            p.sec_off = 0;
            p.size    = kBootRegion;
        } else {
            p.sec_off = sec_off;
            p.size    = size;
            sec_off  += size;
        }
        p.nblocks    = (p.size + kBlock - 1) / kBlock;
        if (p.nblocks == 0) p.nblocks = 1;
        start_block += p.nblocks;
        parts_.push_back(p);

        LOG(Boot, "Imx51NandLayout: id=0x%X block %llu (x%llu) -> .sec 0x%llX size 0x%llX\n",
            p.id, static_cast<unsigned long long>(p.start_block),
            static_cast<unsigned long long>(p.nblocks),
            static_cast<unsigned long long>(p.sec_off),
            static_cast<unsigned long long>(p.size));
    }
}

std::optional<uint64_t> Imx51NandLayout::PhysToSec(uint64_t phys_off) const {
    const uint64_t blk = phys_off / kBlock;
    for (const auto& p : parts_) {
        if (blk < p.start_block || blk >= p.start_block + p.nblocks) continue;
        const uint64_t in_mod = phys_off - p.start_block * kBlock;
        if (in_mod >= p.size) return std::nullopt;   /* past module data -> blank */
        return p.sec_off + in_mod;
    }
    return std::nullopt;                              /* unmapped block -> blank */
}
