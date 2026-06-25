#include "ce_image_relocator.h"

#include "pe_image.h"

#include <cstring>

namespace cerf::ce_image_relocator {

namespace {

uint32_t RvaToFileOff(const PeImage& pe, uint32_t rva) {
    for (const auto& s : pe.Sections()) {
        const uint32_t span = (s.vsize > s.psize) ? s.vsize : s.psize;
        if (rva >= s.rva && rva < s.rva + span) {
            return s.pe_file_off + (rva - s.rva);
        }
    }
    return 0;
}

int SectionIndexForRva(const PeImage& pe, uint32_t rva) {
    const auto& secs = pe.Sections();
    for (size_t i = 0; i < secs.size(); ++i) {
        const uint32_t span = (secs[i].vsize > secs[i].psize) ? secs[i].vsize
                                                              : secs[i].psize;
        if (rva >= secs[i].rva && rva < secs[i].rva + span) return int(i);
    }
    return -1;
}

bool IsMipsMachine(uint16_t m) {
    return m == 0x162 || m == 0x166 || m == 0x168 || m == 0x169 ||
           m == 0x266 || m == 0x366 || m == 0x466;
}

uint16_t Read16(const std::vector<uint8_t>& b, uint32_t off) {
    uint16_t v; std::memcpy(&v, b.data() + off, 2); return v;
}
uint32_t Read32(const std::vector<uint8_t>& b, uint32_t off) {
    uint32_t v; std::memcpy(&v, b.data() + off, 4); return v;
}
void Write16(std::vector<uint8_t>& b, uint32_t off, uint16_t v) {
    std::memcpy(b.data() + off, &v, 2);
}
void Write32(std::vector<uint8_t>& b, uint32_t off, uint32_t v) {
    std::memcpy(b.data() + off, &v, 4);
}

}

void ApplyRelocations(std::vector<uint8_t>& bytes,
                      const PeImage& pe,
                      const std::vector<uint32_t>& section_realaddr,
                      int32_t code_delta,
                      uint32_t& out_patched, uint32_t& out_unhandled) {
    out_patched   = 0;
    out_unhandled = 0;
    constexpr int kFixDirIdx = 5;   /* IMAGE_DIRECTORY_ENTRY_BASERELOC */
    const uint32_t reloc_rva  = pe.DirRva(kFixDirIdx);
    const uint32_t reloc_size = pe.DirSize(kFixDirIdx);
    if (!reloc_size) return;
    const uint32_t reloc_off = RvaToFileOff(pe, reloc_rva);
    if (!reloc_off) return;

    const bool is_mips = IsMipsMachine(pe.Machine());
    uint32_t hi_off  = 0;       /* MIPS HIGH/LOW pairing: file off of the saved hi word */
    bool     have_hi = false;

    uint32_t cursor = reloc_off;
    const uint32_t end = reloc_off + reloc_size;
    while (cursor + 8 <= end && cursor + 8 <= bytes.size()) {
        uint32_t page_va, block_size;
        std::memcpy(&page_va,    bytes.data() + cursor,     4);
        std::memcpy(&block_size, bytes.data() + cursor + 4, 4);
        if (block_size < 8 || cursor + block_size > end) break;

        const uint32_t entry_count = (block_size - 8) / 2;
        for (uint32_t i = 0; i < entry_count; i++) {
            uint16_t entry;
            std::memcpy(&entry, bytes.data() + cursor + 8 + i*2, 2);
            const uint32_t type   = (entry >> 12) & 0xF;
            const uint32_t offset = entry & 0xFFF;
            if (type == 0) continue;            /* IMAGE_REL_BASED_ABSOLUTE - padding */

            const uint32_t tgt_rva = page_va + offset;
            const uint32_t tgt_off = RvaToFileOff(pe, tgt_rva);

            if (type == 3) {                    /* IMAGE_REL_BASED_HIGHLOW */
                if (tgt_off && tgt_off + 4 <= bytes.size()) {
                    uint32_t v = Read32(bytes, tgt_off);
                    /* v is a link-time absolute pointer; rebase by the runtime
                       address of the section it points into (writable data ->
                       slot-0, code/RO -> slot-1), else by code_delta. */
                    const uint32_t pointed_rva = v - pe.ImageBase();
                    const int sec = SectionIndexForRva(pe, pointed_rva);
                    if (sec >= 0 && size_t(sec) < section_realaddr.size()) {
                        v = section_realaddr[size_t(sec)]
                          + (pointed_rva - pe.Sections()[size_t(sec)].rva);
                    } else {
                        v = uint32_t(int64_t(v) + code_delta);
                    }
                    Write32(bytes, tgt_off, v);
                    ++out_patched;
                }
                continue;
            }

            if (is_mips) {
                if (type == 1) {                /* IMAGE_REL_BASED_HIGH - pair with next LOW */
                    hi_off  = tgt_off;
                    have_hi = (tgt_off != 0);
                    ++out_patched;
                    continue;
                }
                if (type == 2) {                /* IMAGE_REL_BASED_LOW */
                    if (tgt_off && tgt_off + 2 <= bytes.size()) {
                        const uint16_t lo = Read16(bytes, tgt_off);
                        if (have_hi && hi_off + 2 <= bytes.size()) {
                            const uint16_t hi = Read16(bytes, hi_off);
                            const uint32_t fv =
                                (uint32_t(hi) << 16) + lo + uint32_t(code_delta);
                            Write16(bytes, hi_off, uint16_t((fv + 0x8000u) >> 16));
                            Write16(bytes, tgt_off, uint16_t(fv & 0xFFFFu));
                        } else {
                            const uint32_t fv =
                                uint32_t(int32_t(int16_t(lo)) + code_delta);
                            Write16(bytes, tgt_off, uint16_t(fv & 0xFFFFu));
                        }
                        ++out_patched;
                    }
                    have_hi = false;
                    continue;
                }
                if (type == 4) {                /* IMAGE_REL_BASED_HIGHADJ - next entry = raw low */
                    uint16_t low_raw = 0;
                    if (i + 1 < entry_count &&
                        cursor + 8 + (i+1)*2 + 2 <= bytes.size())
                        low_raw = Read16(bytes, cursor + 8 + (i+1)*2);
                    if (tgt_off && tgt_off + 2 <= bytes.size()) {
                        uint16_t hi = Read16(bytes, tgt_off);
                        hi = uint16_t(hi + uint16_t(
                            (int32_t(int16_t(low_raw)) + code_delta + 0x8000) >> 16));
                        Write16(bytes, tgt_off, hi);
                        ++out_patched;
                    }
                    ++i;                        /* consume the raw-low slot */
                    continue;
                }
                if (type == 5) {                /* IMAGE_REL_BASED_MIPS_JMPADDR */
                    if (tgt_off && tgt_off + 4 <= bytes.size()) {
                        const uint32_t instr = Read32(bytes, tgt_off);
                        const uint32_t fv =
                            (instr & 0x03FFFFFFu) + uint32_t(code_delta >> 2);
                        Write32(bytes, tgt_off,
                                (instr & 0xFC000000u) | (fv & 0x03FFFFFFu));
                        ++out_patched;
                    }
                    continue;
                }
            }
            ++out_unhandled;
        }
        cursor += block_size;
    }
}

}
