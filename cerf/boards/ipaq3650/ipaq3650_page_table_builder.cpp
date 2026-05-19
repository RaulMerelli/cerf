#include "../../socs/page_table_builder.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_detector.h"
#include "../../cpu/emulated_memory.h"

#include <cstdint>
#include <vector>

namespace {

constexpr uint32_t MB(uint32_t mb) { return mb * 0x100000u; }

enum class OatKind { Dram, Flash, Mmio };

struct OatEntry {
    uint32_t va_base;
    uint32_t pa_base;
    uint32_t size;
    OatKind  kind;
};

/* OEMAddressTable verbatim from nk.exe + 0x13F4 (IDA), 18 entries. */
constexpr OatEntry kOat[] = {
    /*  1 */ { 0x80000000u, 0x00000000u, MB(32), OatKind::Flash }, /* boot ROM (kernel image at PA 0x40000) */
    /*  2 */ { 0x8C000000u, 0xC0000000u, MB(64), OatKind::Dram  }, /* SDRAM bank 0 (kernel ulRAMStart=0x8C0A0000) */
    /*  3 */ { 0x92000000u, 0x10000000u, MB(32), OatKind::Mmio  },
    /*  4 */ { 0x8BA00000u, 0x20000000u, MB( 2), OatKind::Mmio  },
    /*  5 */ { 0x8BC00000u, 0x30000000u, MB( 2), OatKind::Mmio  },
    /*  6 */ { 0x90000000u, 0x28000000u, MB( 8), OatKind::Mmio  },
    /*  7 */ { 0x94400000u, 0x38000000u, MB( 8), OatKind::Mmio  },
    /*  8 */ { 0x94C00000u, 0x2C000000u, MB(64), OatKind::Mmio  },
    /*  9 */ { 0x98C00000u, 0x3C000000u, MB(64), OatKind::Mmio  },
    /* 10 */ { 0x88000000u, 0x80000000u, MB( 4), OatKind::Mmio  },
    /* 11 */ { 0x89000000u, 0x90000000u, MB( 4), OatKind::Mmio  },
    /* 12 */ { 0x8A000000u, 0xA0000000u, MB( 4), OatKind::Mmio  },
    /* 13 */ { 0x8B000000u, 0xB0000000u, MB( 4), OatKind::Mmio  },
    /* 14 */ { 0x88C00000u, 0xE0000000u, MB( 4), OatKind::Mmio  },
    /* 15 */ { 0x88700000u, 0x49000000u, MB( 1), OatKind::Mmio  },
    /* 16 */ { 0x88800000u, 0x4A000000u, MB( 1), OatKind::Mmio  },
    /* 17 */ { 0x88500000u, 0x19000000u, MB( 1), OatKind::Mmio  },
    /* 18 */ { 0x88600000u, 0x1A000000u, MB( 1), OatKind::Mmio  },
};

constexpr uint32_t kDramPaBase     = 0xC0000000u;
constexpr uint32_t kDramSize       = MB(64);
constexpr uint32_t kInitStackTopPa = kDramPaBase + kDramSize;

class Ipaq3650PageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        return emu_.Get<BoardDetector>().GetBoard() == Board::Ipaq3650;
    }

    uint32_t InitStackTopPa() const override { return kInitStackTopPa; }
    uint32_t VaToPa(uint32_t va) const override;
    std::vector<DramRegion>   CachedDramRegions()   const override;
    std::vector<BackedRegion> BackedMemoryRegions() const override;
};

uint32_t Ipaq3650PageTableBuilder::VaToPa(uint32_t va) const {
    for (const auto& e : kOat) {
        if (va >= e.va_base && va < e.va_base + e.size) {
            return e.pa_base + (va - e.va_base);
        }
    }
    LOG(Caution, "Ipaq3650PageTableBuilder::VaToPa: VA 0x%08X outside "
            "every OAT band (nk.exe + 0x13F4)\n", va);
    CerfFatalExit(1);
}

std::vector<DramRegion> Ipaq3650PageTableBuilder::CachedDramRegions() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Dram) {
            regions.push_back({ e.va_base, e.pa_base, e.size });
        }
    }
    return regions;
}

std::vector<BackedRegion>
Ipaq3650PageTableBuilder::BackedMemoryRegions() const {
    std::vector<BackedRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Mmio) continue;
        const DWORD protect =
            (e.kind == OatKind::Flash) ? PAGE_READONLY : PAGE_READWRITE;
        regions.push_back({ e.va_base, e.pa_base, e.size, protect });
    }
    return regions;
}

}  /* namespace */

REGISTER_SERVICE_AS(Ipaq3650PageTableBuilder, PageTableBuilder);
