#include "../page_table_builder.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../board_context.h"

#include <cstdint>
#include <vector>

namespace {

/* Kernel runs XIP from ROM (ROMHDR physfirst = kseg0 0x9F000000 -> PA
   0x1F000000): the ROM flat is a separate PAGE_EXECUTE_READ backing region, not
   in CachedDramRegions - omit the region and flat placement faults. Vr4102-um Tbl 5-6. */
constexpr uint32_t kDramVaBase = 0x80000000u;
constexpr uint32_t kDramPaBase = 0x00000000u;
constexpr uint32_t kDramSize   = 0x00800000u;   /* 8 MB (ROMHDR ulRAMEnd) */

constexpr uint32_t kRomVaBase  = 0x9F000000u;   /* ROMHDR physfirst (kseg0) */
constexpr uint32_t kRomPaBase  = 0x1F000000u;   /* ROM space */
constexpr uint32_t kRomSize    = 0x01000000u;   /* 16 MB (nk.bin flat XIP) */

constexpr uint32_t kKseg0Base  = 0x80000000u;
constexpr uint32_t kKseg2Base  = 0xC0000000u;
constexpr uint32_t kUnmaskKseg = 0x1FFFFFFFu;

class NecMobilePro700PageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::NecMobilePro700;
    }

    uint32_t VaToPa(uint32_t va) const override {
        if (va >= kKseg0Base && va < kKseg2Base) {
            return va & kUnmaskKseg;
        }
        LOG(Caution, "NecMobilePro700PageTableBuilder::VaToPa: VA 0x%08X is "
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

REGISTER_SERVICE_AS(NecMobilePro700PageTableBuilder, PageTableBuilder);
