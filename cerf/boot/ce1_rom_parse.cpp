#define NOMINMAX

#include "ce1_rom_parse.h"

#include "rom_image_parse.h"

#include "../core/log.h"

#include <algorithm>

namespace cerf::ce1_rom_parse {

using cerf::rom_image_parse::ParseRomHdr;
using cerf::rom_image_parse::U32;
using cerf::rom_image_parse::kRomHdrSize;

namespace {

bool ReadInlineName(std::span<const uint8_t> flat, size_t off, std::string& out) {
    out.clear();
    const size_t end = std::min(off + kCe1NameMax, flat.size());
    for (size_t i = off; i < end; ++i) {
        const uint8_t c = flat[i];
        if (c == 0) return !out.empty();
        if (c < 0x20 || c > 0x7E) return false;
        out.push_back(char(c));
    }
    return false;
}

bool VaToOff(std::span<const uint8_t> flat, uint32_t va, uint32_t base_va,
             size_t need, size_t& out) {
    if (va < base_va) return false;
    const size_t off = size_t(va - base_va);
    if (off + need > flat.size()) return false;
    out = off;
    return true;
}

}  /* namespace */

std::vector<size_t> FindAllCe1Romhdrs(std::span<const uint8_t> flat) {
    std::vector<size_t> hits;
    for (size_t off = 0; off + kRomHdrSize <= flat.size(); off += 4) {
        ParsedROMHDR h;
        if (!ParseRomHdr(flat, off, h)) continue;
        if (h.nummods == 0) continue;
        if (h.physlast <= h.physfirst) continue;
        if (h.physlast - h.physfirst > flat.size()) continue;

        const size_t toc0   = off + kRomHdrSize;
        const uint64_t arrays = uint64_t(h.nummods)  * kCe1TocEntrySize
                              + uint64_t(h.numfiles) * kCe1FileEntrySize;
        if (toc0 + arrays > flat.size()) continue;

        std::string name;
        if (!ReadInlineName(flat, toc0 + kCe1TocOffName, name)) continue;

        hits.push_back(off);
    }
    return hits;
}

bool ValidateCe1Xip(std::span<const uint8_t> flat,
                    size_t                   romhdr_off,
                    uint32_t                 base_va,
                    const ParsedROMHDR&      romhdr) {
    const size_t toc0 = romhdr_off + kRomHdrSize;
    if (toc0 + kCe1TocEntrySize > flat.size()) return false;

    if (romhdr.physfirst < base_va) return false;
    if (uint64_t(romhdr.physlast) - base_va > flat.size()) return false;

    const uint32_t nt_va = U32(flat.data(), toc0 + kCe1TocOffNtHeaders);
    size_t nt_off = 0;
    if (!VaToOff(flat, nt_va, base_va, 4, nt_off)) return false;
    if (U32(flat.data(), nt_off) != kPeSignature) return false;

    if (romhdr.ulCopyEntries != 0) {
        const uint64_t arrays_end = uint64_t(base_va) + toc0
            + uint64_t(romhdr.nummods)  * kCe1TocEntrySize
            + uint64_t(romhdr.numfiles) * kCe1FileEntrySize;
        if (arrays_end != romhdr.ulCopyOffset) return false;
    }
    return true;
}

void ParseCe1ModulesAndFiles(std::span<const uint8_t> flat,
                             size_t                   romhdr_off,
                             const ParsedROMHDR&      romhdr,
                             ParsedTOC&               toc) {
    size_t off = romhdr_off + kRomHdrSize;

    toc.modules.reserve(romhdr.nummods);
    for (uint32_t i = 0; i < romhdr.nummods; ++i, off += kCe1TocEntrySize) {
        if (off + kCe1TocEntrySize > flat.size()) {
            LOG(Caution, "CE1 TOCentry %u runs past the ROM image\n", i);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        const uint8_t* p = flat.data() + off;

        ParsedTOCentry m;
        m.dwFileAttributes = U32(p, 0x00);
        m.ftTime           = uint64_t(U32(p, 0x04))
                           | (uint64_t(U32(p, 0x08)) << 32);
        m.nFileSize        = U32(p, kCe1TocOffFileSize);
        m.ulNtHeadersVa    = U32(p, kCe1TocOffNtHeaders);
        m.ulSecHeadersVa   = U32(p, kCe1TocOffSecHeaders);
        m.ulLoadOffset     = U32(p, kCe1TocOffLoadVa);
        ReadInlineName(flat, off + kCe1TocOffName, m.lpszFileName);
        toc.modules.push_back(std::move(m));
    }

    toc.files.reserve(romhdr.numfiles);
    for (uint32_t i = 0; i < romhdr.numfiles; ++i, off += kCe1FileEntrySize) {
        if (off + kCe1FileEntrySize > flat.size()) {
            LOG(Caution, "CE1 FILESentry %u runs past the ROM image\n", i);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        const uint8_t* p = flat.data() + off;

        ParsedFILESentry f;
        f.dwFileAttributes = U32(p, 0x00);
        f.ftTime           = uint64_t(U32(p, 0x04))
                           | (uint64_t(U32(p, 0x08)) << 32);
        f.nRealFileSize    = U32(p, kCe1FileOffRealSize);
        f.nCompFileSize    = U32(p, kCe1FileOffCompSize);
        f.ulLoadOffset     = U32(p, kCe1FileOffLoadVa);
        ReadInlineName(flat, off + kCe1FileOffName, f.lpszFileName);
        toc.files.push_back(std::move(f));
    }
}

bool ResolveCe1Xips(std::span<const uint8_t>      flat,
                    std::vector<ParsedXipRegion>& out_xips,
                    uint32_t&                     out_base_va) {
    struct Cand { size_t off; ParsedROMHDR hdr; };
    std::vector<Cand> cands;
    for (size_t off : FindAllCe1Romhdrs(flat)) {
        Cand c{off, {}};
        if (!ParseRomHdr(flat, off, c.hdr)) continue;
        cands.push_back(std::move(c));
    }

    uint32_t base_va   = 0;
    bool     have_base = false;
    for (const auto& c : cands) {
        if (!ValidateCe1Xip(flat, c.off, c.hdr.physfirst, c.hdr)) continue;
        base_va   = c.hdr.physfirst;
        have_base = true;
        break;
    }
    if (!have_base) return false;

    for (const auto& c : cands) {
        if (!ValidateCe1Xip(flat, c.off, base_va, c.hdr)) continue;
        ParsedXipRegion xip;
        xip.toc.romhdr    = c.hdr;
        xip.toc.romhdr_va = base_va + uint32_t(c.off);
        xip.load_offset   = base_va;
        ParseCe1ModulesAndFiles(flat, c.off, c.hdr, xip.toc);
        out_xips.push_back(std::move(xip));
    }
    if (out_xips.empty()) return false;

    std::sort(out_xips.begin(), out_xips.end(),
              [](const ParsedXipRegion& a, const ParsedXipRegion& b) {
                  return a.toc.romhdr.physfirst < b.toc.romhdr.physfirst;
              });
    out_base_va = base_va;
    return true;
}

}  /* namespace cerf::ce1_rom_parse */
