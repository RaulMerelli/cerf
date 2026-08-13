#include "../page_table_builder.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../board_context.h"

#include <cstdint>
#include <vector>

namespace {

/* DRAM space PA 0x00000000-0x03FFFFFF (VR4111 UM Table 6-6). The E-55 ships 16 MB
   of it: "NEC VR4111 69MHz", "16MB" (PC Watch,
   https://pc.watch.impress.co.jp/docs/article/981203/casio.htm). */
constexpr uint32_t kDramVaBase   = 0x80000000u;
constexpr uint32_t kDramPaBase   = 0x00000000u;
constexpr uint32_t kDramSize     = 0x01000000u;
constexpr uint32_t kDramSpanSize = 0x04000000u;

/* ROM space PA 0x18000000-0x1FFFFFFF (VR4111 UM Table 6-6). The dump's second
   XIP (ROMHDR physfirst 0x9FC00000, nummods 1) sits at the MIPS reset vector
   PA 0x1FC00000. */
constexpr uint32_t kRomVaBase  = 0x9E800000u;
constexpr uint32_t kRomPaBase  = 0x1E800000u;
constexpr uint32_t kRomSize    = 0x01800000u;

constexpr uint32_t kKseg0Base  = 0x80000000u;
constexpr uint32_t kKseg2Base  = 0xC0000000u;
constexpr uint32_t kUnmaskKseg = 0x1FFFFFFFu;

class CasioCassiopeiaE55PageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::CasioCassiopeiaE55;
    }

    uint32_t VaToPa(uint32_t va) const override {
        if (va >= kKseg0Base && va < kKseg2Base) {
            return va & kUnmaskKseg;
        }
        LOG(Caution, "CasioCassiopeiaE55PageTableBuilder::VaToPa: VA 0x%08X is "
                "outside the kseg0/kseg1 unmapped windows\n", va);
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
            { kDramVaBase, kDramPaBase, kDramSpanSize },
            { kRomVaBase,  kRomPaBase,  kRomSize      },
        };
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioCassiopeiaE55PageTableBuilder, PageTableBuilder);
