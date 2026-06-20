#include "../page_table_builder.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"

#include <cstdint>
#include <vector>

namespace {

constexpr uint32_t MB(uint32_t mb) { return mb * 0x100000u; }

enum class OatKind { Dram, Mmio };

struct OatEntry {
    uint32_t va_base;
    uint32_t pa_base;
    uint32_t size;
    OatKind  kind;
};

/* OEMAddressTable from MP377 nk.exe at VA 0x80409F00. The first entry maps a
   256 MB static VA band, but ROMHDR ulRAMEnd is 0x8F400000; only that populated
   244 MB portion is host-backed. The remaining entries are IOP13xx PBI, PCI,
   PCIe, and PMMR windows.

   Each cached static band (0x8/0x9) has an uncached alias at +0x20000000
   (0xA/0xB) per the WinCE ARM convention. The P377 OAL reaches board registers
   through the uncached aliases: HWI_Init (nk.exe sub_80446218) reads the HWI
   block pointer from the FPGA scratch register at uncached VA 0xB0000020
   (PA 0xF0000020) and OALPAtoVA hands back uncached VAs, so both aliases must
   resolve or the early copy reads stray DRAM and the bspio boot-state probe
   faults. CERF does not model cacheability, so the aliases share PA backing. */
constexpr OatEntry kOat[] = {
    { 0x80000000u, 0x00000000u, MB(256), OatKind::Dram },
    { 0x90000000u, 0xF0000000u, MB( 16), OatKind::Mmio },
    { 0x91000000u, 0xF2000000u, MB( 32), OatKind::Mmio },
    { 0x93000000u, 0xFF000000u, MB( 16), OatKind::Mmio },
    { 0x94000000u, 0xC0000000u, MB(128), OatKind::Mmio },
    { 0x9C000000u, 0xD0000000u, MB( 64), OatKind::Mmio },
    { 0xA0000000u, 0x00000000u, MB(256), OatKind::Dram },
    { 0xB0000000u, 0xF0000000u, MB( 16), OatKind::Mmio },
    { 0xB1000000u, 0xF2000000u, MB( 32), OatKind::Mmio },
    { 0xB3000000u, 0xFF000000u, MB( 16), OatKind::Mmio },
    { 0xB4000000u, 0xC0000000u, MB(128), OatKind::Mmio },
    { 0xBC000000u, 0xD0000000u, MB( 64), OatKind::Mmio },
};

constexpr uint32_t kBackedDramSize = 0x0F400000u;

class SiemensMp377PageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::SiemensMP377;
    }

    uint32_t InitStackTopPa() const override { return kBackedDramSize; }
    uint32_t VaToPa(uint32_t va) const override;
    std::vector<DramRegion>   CachedDramRegions() const override;
    std::vector<BackedRegion> BackedMemoryRegions() const override;
    std::vector<DramRegion>   MappedVaSpans() const override;
};

uint32_t SiemensMp377PageTableBuilder::VaToPa(uint32_t va) const {
    for (const auto& e : kOat) {
        if (va >= e.va_base && va < e.va_base + e.size) {
            return e.pa_base + (va - e.va_base);
        }
    }
    LOG(Caution, "SiemensMp377PageTableBuilder::VaToPa: VA 0x%08X outside "
                 "MP377 OEMAddressTable (nk.exe 0x80409F00)\n", va);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

std::vector<DramRegion>
SiemensMp377PageTableBuilder::CachedDramRegions() const {
    return {{ 0x80000000u, 0x00000000u, kBackedDramSize }};
}

std::vector<BackedRegion>
SiemensMp377PageTableBuilder::BackedMemoryRegions() const {
    return {{ 0x80000000u, 0x00000000u, kBackedDramSize, PAGE_READWRITE }};
}

std::vector<DramRegion>
SiemensMp377PageTableBuilder::MappedVaSpans() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        regions.push_back({ e.va_base, e.pa_base, e.size });
    }
    return regions;
}

}  /* namespace */

REGISTER_SERVICE_AS(SiemensMp377PageTableBuilder, PageTableBuilder);
