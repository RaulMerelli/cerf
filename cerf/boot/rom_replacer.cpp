#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX

#include "pe_image.h"
#include "rom_parser_service.h"
#include "rom_placer.h"

#include "../core/cerf_emulator.h"
#include "../core/device_config.h"
#include "../core/log.h"
#include "../core/service.h"
#include "../core/string_utils.h"
#include "../cpu/emulated_memory.h"
#include "../socs/page_table_builder.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kPageMask = 0xFFFu;

constexpr uint32_t kRomHdrSize    = 84;
constexpr uint32_t kTocEntrySize  = 32;
constexpr uint32_t kE32RomSize    = 110;
constexpr uint32_t kO32RomSize    = 24;

constexpr uint32_t kHdrRAMFreeOff    = 0x18;

constexpr uint32_t kTocOffNFileSize  = 0x0C;
constexpr uint32_t kTocOffE32Offset  = 0x14;
constexpr uint32_t kTocOffO32Offset  = 0x18;
constexpr uint32_t kTocOffLoadOffset = 0x1C;

constexpr uint32_t kE32OffObjcnt        = 0x00;
constexpr uint32_t kE32OffImageFlags    = 0x02;
constexpr uint32_t kE32OffEntryRva      = 0x04;
constexpr uint32_t kE32OffVbase         = 0x08;
constexpr uint32_t kE32OffSubsysMajor   = 0x0C;
constexpr uint32_t kE32OffSubsysMinor   = 0x0E;
constexpr uint32_t kE32OffStackMax      = 0x10;
constexpr uint32_t kE32OffVsize         = 0x14;
constexpr uint32_t kE32OffSect14Rva     = 0x18;
constexpr uint32_t kE32OffSect14Size    = 0x1C;
constexpr uint32_t kE32OffTimestamp     = 0x20;
constexpr uint32_t kE32OffUnit          = 0x24;
constexpr uint32_t kE32OffSubsys        = 0x6C;

constexpr uint32_t kO32OffVsize    = 0;
constexpr uint32_t kO32OffRva      = 4;
constexpr uint32_t kO32OffPsize    = 8;
constexpr uint32_t kO32OffDataptr  = 12;
constexpr uint32_t kO32OffRealaddr = 16;
constexpr uint32_t kO32OffFlags    = 20;

constexpr int kE32UnitCount = 9;

uint32_t AlignPage(uint32_t pa) {
    return (pa + kPageMask) & ~kPageMask;
}

class RomReplacer : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().poc_rom_injection;
    }

    void OnReady() override {
        emu_.Get<RomPlacer>();    /* DRAM populated before we mutate */

        auto& parser = emu_.Get<RomParserService>();
        if (!parser.Ok()) {
            LOG(Caution, "[RomReplacer] parser not ready, skipping PoC\n");
            return;
        }

        const std::string source_path =
            GetCerfDir() + "platform\\prebuilt\\sampleapp.exe";

        for (const char* victim : { "explorer.exe", "shell32.exe" }) {
            if (!Replace(victim, source_path)) {
                LOG(Caution, "[RomReplacer] PoC replacement of %s with "
                        "%s did not complete\n", victim, source_path.c_str());
            }
        }
    }

private:
    bool Replace(const char* victim_name, const std::string& source_path);

    bool FindVictim(const char* name, size_t& out_index, uint32_t& out_entry_kva);
    uint32_t KvaForPa(uint32_t pa);
    static uint32_t SectionStepPa(uint32_t cursor, uint32_t psize) {
        return cursor + AlignPage(psize);
    }

    void     WriteE32Rom    (uint32_t pa, const PeImage& pe);
    void     WriteO32Array  (uint32_t pa, const PeImage& pe,
                              uint32_t section_pa_base);
    uint32_t WriteSectionBytes(uint32_t pa, const PeImage& pe);
};

bool RomReplacer::FindVictim(const char* name, size_t& out_index,
                              uint32_t& out_entry_kva) {
    auto& parser = emu_.Get<RomParserService>();
    const auto& rom = parser.Primary();
    if (rom.xips.empty()) return false;
    const auto& toc = rom.xips[0].toc;
    for (size_t i = 0; i < toc.modules.size(); ++i) {
        if (_stricmp(toc.modules[i].lpszFileName.c_str(), name) == 0) {
            out_index     = i;
            out_entry_kva = toc.romhdr_va + kRomHdrSize
                          + uint32_t(i) * kTocEntrySize;
            return true;
        }
    }
    return false;
}

uint32_t RomReplacer::KvaForPa(uint32_t pa) {
    auto& pt = emu_.Get<PageTableBuilder>();
    for (const auto& band : pt.CachedDramRegions()) {
        if (pa >= band.pa_base && pa < band.pa_base + band.size) {
            return band.va_base + (pa - band.pa_base);
        }
    }
    LOG(Caution, "[RomReplacer] PA 0x%08X falls outside every cached "
            "DRAM band; cannot compute kernel-VA\n", pa);
    return 0;
}

void RomReplacer::WriteE32Rom(uint32_t pa, const PeImage& pe) {
    auto& mem = emu_.Get<EmulatedMemory>();

    mem.WriteHalf(pa + kE32OffObjcnt,      uint16_t(pe.Sections().size()));
    mem.WriteHalf(pa + kE32OffImageFlags,  pe.ImageFlags());
    mem.WriteWord(pa + kE32OffEntryRva,    pe.EntryRva());
    mem.WriteWord(pa + kE32OffVbase,       pe.ImageBase());
    mem.WriteHalf(pa + kE32OffSubsysMajor, pe.SubsysMajor());
    mem.WriteHalf(pa + kE32OffSubsysMinor, pe.SubsysMinor());
    mem.WriteWord(pa + kE32OffStackMax,    pe.StackReserve());
    mem.WriteWord(pa + kE32OffVsize,       pe.ImageSize());
    mem.WriteWord(pa + kE32OffSect14Rva,   0);
    mem.WriteWord(pa + kE32OffSect14Size,  0);
    mem.WriteWord(pa + kE32OffTimestamp,   0);
    for (int i = 0; i < kE32UnitCount; ++i) {
        mem.WriteWord(pa + kE32OffUnit + uint32_t(i) * 8u + 0, pe.DirRva (i));
        mem.WriteWord(pa + kE32OffUnit + uint32_t(i) * 8u + 4, pe.DirSize(i));
    }
    mem.WriteHalf(pa + kE32OffSubsys,      pe.Subsystem());
}

void RomReplacer::WriteO32Array(uint32_t pa, const PeImage& pe,
                                 uint32_t section_pa_base) {
    auto& mem = emu_.Get<EmulatedMemory>();
    uint32_t section_pa = section_pa_base;
    for (size_t i = 0; i < pe.Sections().size(); ++i) {
        const auto& s = pe.Sections()[i];
        const uint32_t kva = KvaForPa(section_pa);
        const uint32_t off = pa + uint32_t(i) * kO32RomSize;

        const uint32_t realaddr = pe.ImageBase() + s.rva;
        mem.WriteWord(off + kO32OffVsize,    s.vsize);
        mem.WriteWord(off + kO32OffRva,      s.rva);
        mem.WriteWord(off + kO32OffPsize,    s.psize);
        mem.WriteWord(off + kO32OffDataptr,  kva);
        /* MUST equal image_base+rva — NK maps the section at this VA
           verbatim (loader.c:2606); wrong value faults the loader. */
        mem.WriteWord(off + kO32OffRealaddr, realaddr);
        mem.WriteWord(off + kO32OffFlags,    s.flags);

        LOG(Boot, "[RomReplacer]   o32[%zu] vsize=0x%05X rva=0x%05X "
                  "psize=0x%05X dataptr=0x%08X realaddr=0x%08X flags=0x%08X\n",
            i, s.vsize, s.rva, s.psize, kva, realaddr, s.flags);

        section_pa = SectionStepPa(section_pa, s.psize);
    }
}

uint32_t RomReplacer::WriteSectionBytes(uint32_t pa, const PeImage& pe) {
    auto& mem = emu_.Get<EmulatedMemory>();
    const auto& pe_bytes = pe.Bytes();

    uint32_t cursor = pa;
    for (const auto& s : pe.Sections()) {
        if (s.psize > 0 && size_t(s.pe_file_off) + s.psize <= pe_bytes.size()) {
            mem.CopyIn(cursor, pe_bytes.data() + s.pe_file_off, s.psize);
        }
        cursor = SectionStepPa(cursor, s.psize);
    }
    return cursor;
}

bool RomReplacer::Replace(const char* victim_name, const std::string& source_path) {
    size_t   victim_idx = 0;
    uint32_t entry_kva  = 0;
    if (!FindVictim(victim_name, victim_idx, entry_kva)) {
        LOG(Boot, "[RomReplacer] '%s' not present in TOC — skip\n", victim_name);
        return true;
    }

    std::ifstream f(source_path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        LOG(Caution, "[RomReplacer] cannot open %s\n", source_path.c_str());
        return false;
    }
    const auto sz = f.tellg();
    std::vector<uint8_t> pe_bytes(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(pe_bytes.data()), sz);
    const size_t pe_bytes_size = pe_bytes.size();

    PeImage pe(std::move(pe_bytes));
    if (!pe.Parsed()) {
        LOG(Caution, "[RomReplacer] PE parse failed for %s\n", source_path.c_str());
        return false;
    }

    auto& parser = emu_.Get<RomParserService>();
    const auto& rom = parser.Primary();
    if (rom.xips.empty()) return false;
    const auto& primary_toc = rom.xips[0].toc;
    auto& pt = emu_.Get<PageTableBuilder>();

    /* MUST anchor past ulRAMFree — `[ulRAMStart, ulRAMFree)` is NK's
       live RW workspace (romimage/module.cpp:1280); any bytes placed
       inside it get clobbered when NK writes its globals mid-boot. */
    const uint32_t orig_ramfree_va = primary_toc.romhdr.ulRAMFree;
    const uint32_t orig_ramfree_pa = pt.VaToPa(orig_ramfree_va);

    const uint32_t e32_pa = AlignPage(orig_ramfree_pa);
    const uint32_t o32_pa = e32_pa + AlignPage(kE32RomSize);
    const uint32_t section_pa_base =
        o32_pa + AlignPage(uint32_t(pe.Sections().size()) * kO32RomSize);

    const uint32_t e32_kva = KvaForPa(e32_pa);
    const uint32_t o32_kva = KvaForPa(o32_pa);
    const uint32_t section_kva_base = KvaForPa(section_pa_base);
    if (!e32_kva || !o32_kva || !section_kva_base) {
        LOG(Caution, "[RomReplacer] appended region does not fit inside a "
                "cached-DRAM band (e32_pa=0x%08X o32_pa=0x%08X "
                "section_pa=0x%08X)\n",
            e32_pa, o32_pa, section_pa_base);
        return false;
    }

    WriteE32Rom  (e32_pa, pe);
    WriteO32Array(o32_pa, pe, section_pa_base);
    const uint32_t after_sections_pa = WriteSectionBytes(section_pa_base, pe);

    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t entry_pa = pt.VaToPa(entry_kva);
    mem.WriteWord(entry_pa + kTocOffNFileSize,  uint32_t(pe_bytes_size));
    mem.WriteWord(entry_pa + kTocOffE32Offset,  e32_kva);
    mem.WriteWord(entry_pa + kTocOffO32Offset,  o32_kva);
    mem.WriteWord(entry_pa + kTocOffLoadOffset, section_kva_base);

    const uint32_t new_ramfree_pa = AlignPage(after_sections_pa);
    const uint32_t new_ramfree_va = KvaForPa(new_ramfree_pa);

    const uint32_t romhdr_pa = pt.VaToPa(primary_toc.romhdr_va);
    mem.WriteWord(romhdr_pa + kHdrRAMFreeOff, new_ramfree_va);

    LOG(Boot, "[RomReplacer] %s slot redirected: idx=%zu entry_kva=0x%08X "
              "e32_kva=0x%08X o32_kva=0x%08X load_kva=0x%08X "
              "sections=%zu pe_size=%zu\n",
        victim_name, victim_idx, entry_kva, e32_kva, o32_kva,
        section_kva_base, pe.Sections().size(), pe_bytes_size);
    LOG(Boot, "[RomReplacer] ROMHDR ulRAMFree 0x%08X -> 0x%08X "
              "(physlast 0x%08X / ulRAMStart 0x%08X unchanged)\n",
        orig_ramfree_va, new_ramfree_va,
        primary_toc.romhdr.physlast, primary_toc.romhdr.ulRAMStart);
    return true;
}

}  /* namespace */

REGISTER_SERVICE(RomReplacer);
