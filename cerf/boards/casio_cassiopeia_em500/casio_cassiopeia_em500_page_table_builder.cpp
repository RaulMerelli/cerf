#include "../page_table_builder.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../board_context.h"

#include <cstdint>
#include <vector>

namespace {

/* DRAM area PA 0x00000000-0x07FFFFFF (VR4131 UM Fig 3-1); ROMHDR
   ulRAMStart 0x80051000 / ulRAMEnd 0x81000000 place this board's populated
   DRAM at kseg0 0x80000000, 16 MB. */
constexpr uint32_t kDramVaBase = 0x80000000u;
constexpr uint32_t kDramPaBase = 0x00000000u;
constexpr uint32_t kDramSize   = 0x01000000u;

/* ROM area PA 0x18000000-0x1FFFFFFF (VR4131 UM Fig 3-1). The CE XIP is at
   ROMHDR physfirst = kseg0 0x9F000000 -> PA 0x1F000000; a second XIP ROMHDR
   at flat offset 0xC00040 sits beside the reset vector 0xBFC00000
   (U15509EJ2V0UM p.174). */
constexpr uint32_t kRomVaBase  = 0x9F000000u;
constexpr uint32_t kRomPaBase  = 0x1F000000u;
constexpr uint32_t kRomSize    = 0x01000000u;

constexpr uint32_t kKseg0Base  = 0x80000000u;
constexpr uint32_t kKseg2Base  = 0xC0000000u;
constexpr uint32_t kUnmaskKseg = 0x1FFFFFFFu;

class CasioCassiopeiaEm500PageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::CasioCassiopeiaEm500;
    }

    uint32_t VaToPa(uint32_t va) const override {
        if (va >= kKseg0Base && va < kKseg2Base) {
            return va & kUnmaskKseg;
        }
        LOG(Caution, "CasioCassiopeiaEm500PageTableBuilder::VaToPa: VA 0x%08X is "
                "outside the kseg0/kseg1 unmapped windows (ROM placement only "
                "feeds kseg VAs)\n", va);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    std::vector<DramRegion> CachedDramRegions() const override {
        return { { kDramVaBase, kDramPaBase, kDramSize } };
    }

    std::vector<BackedRegion> BackedMemoryRegions() const override {
        return {
            { kDramVaBase, kDramPaBase, kDramSize, PAGE_READWRITE },
            { kRomVaBase,  kRomPaBase,  kRomSize,  PAGE_EXECUTE_READ },
        };
    }

    std::vector<DramRegion> MappedVaSpans() const override {
        return {
            { kDramVaBase, kDramPaBase, kDramSize },
            { kRomVaBase,  kRomPaBase,  kRomSize  },
        };
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioCassiopeiaEm500PageTableBuilder, PageTableBuilder);
