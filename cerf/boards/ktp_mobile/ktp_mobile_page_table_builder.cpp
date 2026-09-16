#include "../page_table_builder.h"
#include "../../core/fatal.h"

#include "ktp_mobile_oat.h"
#include "ktp_mobile_oat_from_rom.h"

#include "../../boot/rom_parser_service.h"
#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include <cstdint>
#include <vector>

namespace {

/* IMX6SDLRM Rev.1 Table 2-2. */
constexpr uint32_t kBootRomPa = 0x00000000u;
constexpr uint32_t kBootRomSize = 0x00018000u;
constexpr uint32_t kOcramPa = 0x00900000u;
constexpr uint32_t kOcramSize = 0x00020000u;

class KtpMobilePageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && BoardContext::IsKtpMobile(bd->GetBoard());
    }

    uint32_t InitStackTopPa() const override { return kInitStackTopPa; }
    uint32_t VaToPa(uint32_t va) const override;
    std::vector<DramRegion> CachedDramRegions() const override;
    std::vector<BackedRegion> BackedMemoryRegions() const override;
    std::vector<DramRegion> MappedVaSpans() const override;
private:
    const std::vector<KtpMobileOatEntry>& RomSpans() const;

    static constexpr DramRegion kDram{kDramVa, kDramPa, kDramSize};

    mutable std::vector<KtpMobileOatEntry> spans_;
    mutable bool spans_read_ = false;
};

const std::vector<KtpMobileOatEntry>& KtpMobilePageTableBuilder::RomSpans() const {
    if (spans_read_) return spans_;
    const KtpMobileRomOat oat = FindKtpMobileOatInRom(emu_.Get<RomParserService>().Primary().flat);
    if (oat.valid()) {
        spans_ = oat.entries;
        spans_read_ = true;
    }
    return spans_;
}

uint32_t KtpMobilePageTableBuilder::VaToPa(uint32_t va) const {
    if (va >= kDram.va_base && va < kDram.va_base + kDram.size) return kDram.pa_base + (va - kDram.va_base);
    for (const auto& e : RomSpans()) {
        if (va >= e.va && va < e.va + e.size) return e.pa + (va - e.va);
    }
    emu_.Get<Fatal>().Die("KtpMobilePageTableBuilder::VaToPa: VA 0x%08X is outside the cached DDR "
                          "window and every span of the OAL OEMAddressTable the ROM declares",
                          va);
}

std::vector<DramRegion> KtpMobilePageTableBuilder::CachedDramRegions() const {
    return {kDram};
}

std::vector<BackedRegion> KtpMobilePageTableBuilder::BackedMemoryRegions() const {
    std::vector<BackedRegion> regions;
    regions.push_back({kDram.va_base, kDram.pa_base, kDram.size, PAGE_READWRITE});

    /* IMX6SDLRM Rev.4 Table 2-1 maps boot ROM at PA 0 and OCRAM at 0x00900000;
       QEMU fsl-imx6.c exposes the OCRAM and its alias as one backing store. */
    const auto back_on_chip = [&](uint32_t pa, uint32_t size) {
        for (const auto& e : RomSpans()) {
            if (pa < e.pa || pa - e.pa >= e.size) continue;
            regions.push_back({e.va + (pa - e.pa), pa, size, PAGE_READWRITE});
            return;
        }
    };
    for (const auto& e : RomSpans()) {
        if (kBootRomPa < e.pa || kBootRomPa - e.pa >= e.size) continue;
        regions.push_back({e.va + (kBootRomPa - e.pa), kBootRomPa,
                           kBootRomSize, PAGE_EXECUTE_READ});
        break;
    }
    back_on_chip(kOcramPa, kOcramSize);

    return regions;
}

std::vector<DramRegion> KtpMobilePageTableBuilder::MappedVaSpans() const {
    std::vector<DramRegion> regions;
    regions.push_back(kDram);
    for (const auto& e : RomSpans())
        regions.push_back({e.va, e.pa, e.size});
    return regions;
}

}

REGISTER_SERVICE_AS(KtpMobilePageTableBuilder, PageTableBuilder);
