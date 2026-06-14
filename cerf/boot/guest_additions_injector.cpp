#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX

#include "ce_image_relocator.h"
#include "cerf_injection_region.h"
#include "guest_additions_binaries.h"
#include "guest_cold_boot.h"
#include "guest_module_placer.h"
#include "imgfs_injector.h"
#include "pe_image.h"
#include "rom_parser_service.h"
#include "rom_placer.h"
#include "rom_record_layout.h"

#include "../core/cerf_emulator.h"
#include "../core/device_config.h"
#include "../core/log.h"
#include "../core/service.h"
#include "../cpu/emulated_memory.h"
#include "../boards/page_table_builder.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kPageMask = 0xFFFu;

/* The tiny carrier injected as the victim; the full cerf_guest body is served
   separately over the cerf_virt body channel and LoadLibrary'd by the stub. */
constexpr const char* kStubDll = "cerf_guest_stub.dll";

constexpr uint32_t kImgScnMemShared = 0x10000000u;
constexpr uint32_t kImgScnMemWrite  = 0x80000000u;

uint32_t AlignPage(uint32_t v) { return (v + kPageMask) & ~kPageMask; }
uint32_t Align4(uint32_t v)    { return (v + 3u) & ~3u; }

class GuestAdditionsInjector : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions;
    }

    void OnReady() override {
        emu_.Get<RomPlacer>();

        auto& parser = emu_.Get<RomParserService>();
        if (!parser.Ok()) {
            LOG(Caution, "parser not ready, skipping\n");
            return;
        }

        ce_major_ = DetectCeMajor();
        /* e32_rom inserts e32_timestamp before e32_unit in CE5 (pehdr.h: CE3
           e32_unit at 0x20, CE5 adds e32_timestamp at 0x20 pushing it to 0x24),
           so CE3 and CE4.2 use the pre-timestamp layout. */
        layout_ = (ce_major_ <= 4) ? &kE32RomCE3 : &kE32RomCE5plus;
        LOG(GuestAdditions, "CE major=%u; e32_rom layout=%s\n",
            ce_major_, (layout_ == &kE32RomCE3) ? "pre-CE5" : "CE5+");

        const auto& subs = emu_.Get<DeviceConfig>().global_rom_substitutions;
        if (subs.empty()) {
            LOG(Caution, "guest_additions enabled but cerf.json "
                    "global_substitutions_inside_rom is empty\n");
            CerfFatalExit();
        }

        const std::string stub_path =
            emu_.Get<GuestAdditionsBinaries>().StagedPath(kStubDll);

        auto& imgfs = emu_.Get<ImgfsInjector>();
        int matched = 0;
        for (const auto& sub : subs) {
            /* IMGFS injects the SAME stub, not the full body: a kernel-loaded
               full body shares one .data across gwes + the device.exe carrier at
               the high victim vbase (the CE5 loader doesn't per-process it) and
               corrupts gwes's globals; the stub manual-maps the body per-process. */
            if (Replace(sub.first.c_str(), stub_path)) ++matched;
            if (imgfs.ReplaceVictim(sub.first.c_str(), stub_path)) ++matched;
        }
        if (matched == 0) {
            LOG(Caution, "no display-driver victim matched\n");
            CerfFatalExit();
        }
        LOG(GuestAdditions, "%d victim(s) replaced\n", matched);
    }

private:
    bool Replace(const char* victim_name, const std::string& source_path);

    bool FindVictim(const char* name, size_t& out_index, uint32_t& out_entry_kva);
    uint32_t DetectCeMajor();

    void WriteE32Rom(uint32_t pa, const PeImage& pe, uint32_t target_vbase);
    void WriteO32Array(uint32_t pa, const PeImage& pe,
                       const std::vector<uint32_t>& dataptr_kva,
                       const std::vector<uint32_t>& realaddr,
                       const std::vector<uint32_t>& flags);
    void WriteSectionBytes(const std::vector<uint32_t>& sec_pa, const PeImage& pe,
                           const std::vector<uint8_t>& bytes);

    uint32_t            ce_major_ = 5;
    const E32RomLayout* layout_   = &kE32RomCE5plus;
};

uint32_t GuestAdditionsInjector::DetectCeMajor() {
    auto& parser = emu_.Get<RomParserService>();
    auto* nk = parser.KernelModule();
    if (!nk) {
        LOG(Caution, "nk.exe missing from TOC - can't read e32_subsysmajor "
                "and the kernel won't boot without nk anyway; refusing to "
                "guess e32_rom layout\n");
        CerfFatalExit();
    }
    auto& pt  = emu_.Get<PageTableBuilder>();
    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t e32_pa = pt.VaToPa(nk->ulE32Offset);
    const uint16_t subsysmajor = mem.ReadHalf(e32_pa + kE32SubsysmajorOff);
    if (subsysmajor < 3 || subsysmajor > 8) {
        LOG(Caution, "nk.exe e32_subsysmajor=%u outside plausible CE range "
                "(3..8) - refusing to guess e32_rom layout\n", subsysmajor);
        CerfFatalExit();
    }
    return subsysmajor;
}

bool GuestAdditionsInjector::FindVictim(const char* name, size_t& out_index,
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

void GuestAdditionsInjector::WriteE32Rom(uint32_t pa, const PeImage& pe,
                                          uint32_t target_vbase) {
    auto& mem = emu_.Get<EmulatedMemory>();
    const E32RomLayout& L = *layout_;

    mem.WriteHalf(pa + L.off_objcnt,      uint16_t(pe.Sections().size()));
    mem.WriteHalf(pa + L.off_imageflags,  pe.ImageFlags());
    mem.WriteWord(pa + L.off_entryrva,    pe.EntryRva());
    mem.WriteWord(pa + L.off_vbase,       target_vbase);
    mem.WriteHalf(pa + L.off_subsysmajor, pe.SubsysMajor());
    mem.WriteHalf(pa + L.off_subsysminor, pe.SubsysMinor());
    mem.WriteWord(pa + L.off_stackmax,    pe.StackReserve());
    mem.WriteWord(pa + L.off_vsize,       pe.ImageSize());
    mem.WriteWord(pa + L.off_sect14rva,   0);
    mem.WriteWord(pa + L.off_sect14size,  0);
    if (L.off_timestamp >= 0) {
        mem.WriteWord(pa + uint32_t(L.off_timestamp), 0);
    }
    for (int i = 0; i < kE32UnitCount; ++i) {
        mem.WriteWord(pa + L.off_unit + uint32_t(i) * 8u + 0, pe.DirRva (i));
        mem.WriteWord(pa + L.off_unit + uint32_t(i) * 8u + 4, pe.DirSize(i));
    }
    mem.WriteHalf(pa + L.off_subsys,      pe.Subsystem());
}

void GuestAdditionsInjector::WriteO32Array(uint32_t pa, const PeImage& pe,
                                            const std::vector<uint32_t>& dataptr_kva,
                                            const std::vector<uint32_t>& realaddr,
                                            const std::vector<uint32_t>& flags) {
    auto& mem = emu_.Get<EmulatedMemory>();
    for (size_t i = 0; i < pe.Sections().size(); ++i) {
        const auto& s = pe.Sections()[i];
        const uint32_t off = pa + uint32_t(i) * kO32RomSize;
        mem.WriteWord(off + kO32OffVsize,    s.vsize);
        mem.WriteWord(off + kO32OffRva,      s.rva);
        mem.WriteWord(off + kO32OffPsize,    s.psize);
        mem.WriteWord(off + kO32OffDataptr,  dataptr_kva[i]);
        mem.WriteWord(off + kO32OffRealaddr, realaddr[i]);
        mem.WriteWord(off + kO32OffFlags,    flags[i]);
        LOG(GuestAdditions, "  o32[%zu] vsize=0x%05X rva=0x%05X psize=0x%05X "
                  "dataptr=0x%08X realaddr=0x%08X flags=0x%08X\n",
            i, s.vsize, s.rva, s.psize, dataptr_kva[i], realaddr[i], flags[i]);
    }
}

void GuestAdditionsInjector::WriteSectionBytes(const std::vector<uint32_t>& sec_pa,
                                                const PeImage& pe,
                                                const std::vector<uint8_t>& bytes) {
    auto& mem = emu_.Get<EmulatedMemory>();
    for (size_t i = 0; i < pe.Sections().size(); ++i) {
        const auto& s = pe.Sections()[i];
        if (s.psize > 0 && size_t(s.pe_file_off) + s.psize <= bytes.size()) {
            mem.CopyIn(sec_pa[i], bytes.data() + s.pe_file_off, s.psize);
        }
    }
}

bool GuestAdditionsInjector::Replace(const char* victim_name,
                                      const std::string& source_path) {
    size_t   victim_idx = 0;
    uint32_t entry_kva  = 0;
    if (!FindVictim(victim_name, victim_idx, entry_kva)) {
        LOG(GuestAdditions, "'%s' not present in TOC - skip\n", victim_name);
        return false;
    }

    std::ifstream f(source_path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        LOG(Caution, "cannot open %s - cerf_guest_stub.dll must be built and "
                "staged before injecting %s\n", source_path.c_str(), victim_name);
        CerfFatalExit();
    }
    const auto sz = f.tellg();
    std::vector<uint8_t> pe_bytes(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(pe_bytes.data()), sz);
    const size_t pe_bytes_size = pe_bytes.size();

    PeImage pe(std::move(pe_bytes));
    if (!pe.Parsed()) {
        LOG(Caution, "PE parse failed for %s - stub is corrupt; injection of "
                "%s aborted\n", source_path.c_str(), victim_name);
        CerfFatalExit();
    }

    auto& parser = emu_.Get<RomParserService>();
    const auto& rom = parser.Primary();
    if (rom.xips.empty()) {
        LOG(Caution, "primary ROM has no XIP regions - FindVictim matched %s "
                "but the TOC vanished\n", victim_name);
        CerfFatalExit();
    }
    const auto& primary_toc = rom.xips[0].toc;
    auto& pt  = emu_.Get<PageTableBuilder>();
    auto& mem = emu_.Get<EmulatedMemory>();
    const E32RomLayout& L = *layout_;

    /* Fields read here (vbase/imgflags/objcnt) lie in the common prefix shared
       by CE3 and CE5+ layouts. */
    const uint32_t orig_e32_kva  = primary_toc.modules[victim_idx].ulE32Offset;
    const uint32_t orig_e32_pa   = pt.VaToPa(orig_e32_kva);
    const uint32_t orig_o32_kva  = primary_toc.modules[victim_idx].ulO32Offset;
    const uint32_t orig_o32_pa   = pt.VaToPa(orig_o32_kva);
    const uint32_t orig_vbase    = mem.ReadWord(orig_e32_pa + L.off_vbase);
    const uint16_t orig_imgflags = mem.ReadHalf(orig_e32_pa + L.off_imageflags);
    const uint16_t orig_objcnt   = mem.ReadHalf(orig_e32_pa + L.off_objcnt);
    if (orig_objcnt == 0) {
        LOG(Caution, "%s victim has zero sections - no footprint to reuse for "
                "the stub\n", victim_name);
        CerfFatalExit();
    }
    LOG(GuestAdditions, "orig %s e32 @KVA 0x%08X imageflags=0x%04X objcnt=%u "
              "o32 @KVA 0x%08X\n",
        victim_name, orig_e32_kva, orig_imgflags, orig_objcnt, orig_o32_kva);
    for (uint32_t i = 0; i < orig_objcnt && i < 16; ++i) {
        const uint32_t off = orig_o32_pa + i * kO32RomSize;
        LOG(GuestAdditions, "  ORIG o32[%u] vsize=0x%05X rva=0x%05X psize=0x%05X "
                  "dataptr=0x%08X realaddr=0x%08X flags=0x%08X\n",
            i,
            mem.ReadWord(off + kO32OffVsize),
            mem.ReadWord(off + kO32OffRva),
            mem.ReadWord(off + kO32OffPsize),
            mem.ReadWord(off + kO32OffDataptr),
            mem.ReadWord(off + kO32OffRealaddr),
            mem.ReadWord(off + kO32OffFlags));
    }

    const uint32_t code_realaddr0 = mem.ReadWord(orig_o32_pa + kO32OffRealaddr);
    const uint32_t code_rva0      = mem.ReadWord(orig_o32_pa + kO32OffRva);
    const uint32_t codebase       = code_realaddr0 - code_rva0;

    bool vbase_ok = false;
    if (ce_major_ >= 6) {
        const uint32_t um_first = (primary_toc.romhdr.dllfirst >> 16) << 16;
        const uint32_t um_last  = (primary_toc.romhdr.dlllast  >> 16) << 16;
        const uint32_t km_first = (primary_toc.romhdr.dllfirst & 0xFFFFu) << 16;
        const uint32_t km_last  = (primary_toc.romhdr.dlllast  & 0xFFFFu) << 16;
        vbase_ok = (orig_vbase >= um_first && orig_vbase < um_last)
                || (orig_vbase >= km_first && orig_vbase < km_last);
    } else {
        const bool in_dll_region = (orig_vbase >= primary_toc.romhdr.dllfirst)
                                && (orig_vbase <  primary_toc.romhdr.dlllast);
        const bool xip_codebase  = (orig_vbase != 0)
                                && ((orig_vbase & kPageMask) == 0)
                                && (orig_vbase == codebase)
                                && (orig_vbase < primary_toc.romhdr.physfirst);
        vbase_ok = in_dll_region || xip_codebase;
    }
    if (!vbase_ok) {
        LOG(Caution, "%s original vbase 0x%08X fails sanity (CE major=%u, "
                "codebase=0x%08X, dllfirst=0x%08X dlllast=0x%08X) - probable "
                "ROMHDR corruption or unsupported CE version\n",
            victim_name, orig_vbase, ce_major_, codebase,
            primary_toc.romhdr.dllfirst, primary_toc.romhdr.dlllast);
        CerfFatalExit();
    }

    /* Acquire the CERF-owned injection band (lazy; halts if this board's OAT
       leaves no static-window hole). The stub's records + section bytes live
       here, never the victim's section; the TOC is repointed at band VAs the
       MMU walker overlay serves. */
    auto& region = emu_.Get<CerfInjectionRegion>();
    const uint32_t band_va   = region.BandVaBase();
    const uint32_t band_pa   = region.BandPaBase();
    const uint32_t band_size = region.BandSize();

    /* CE6/7 run in place from the band: a static-window kernel-VA base makes
       LoadO32 skip ReadSection (WINCE600/700 loader.c IsKernelVa = [0x80000000,
       0xC0000000)). CE3/4/5 have no such gate -> copy to a section-1 vbase. */
    const bool in_place = (ce_major_ >= 6);

    uint32_t target_vbase = band_va;
    uint32_t run_base     = band_va;   /* base the section bytes are relocated for */
    if (!in_place) {
        const uint32_t slot_base = codebase - orig_vbase;
        target_vbase = emu_.Get<GuestModulePlacer>().ComputeVbase(
            orig_vbase, slot_base, pe.ImageSize(), ce_major_, L.off_vbase,
            victim_name);
        run_base = target_vbase + slot_base;
    }

    /* RVA layout in the band: section i bytes at band+rva, so an in-place
       module runs correctly at vbase=band_va with section i at band_va+rva.
       dataptr is always the band VA (overlay-served source). */
    const size_t nsec = pe.Sections().size();
    std::vector<uint32_t> sec_pa(nsec), dataptr(nsec), realaddr(nsec), flags(nsec);
    for (size_t i = 0; i < nsec; ++i) {
        const auto& s = pe.Sections()[i];
        sec_pa[i]   = band_pa + s.rva;
        dataptr[i]  = band_va + s.rva;
        realaddr[i] = run_base + s.rva;
        /* Copy path MUST force MEM_WRITE (else RO-XIP VirtualCopy resolves
           dataptr via guest page tables, cannot reach the overlay band; CE5
           loader.c:3040) + SHARED (else per-processed to a slot base on the
           device.exe carrier load; CE5 loader.c:3086). In place: orig flags. */
        flags[i] = in_place
            ? emu_.Get<GuestModulePlacer>().EffSectionFlags(s.flags)
            : (s.flags | kImgScnMemWrite | kImgScnMemShared);
    }

    /* Records after the full virtual image (overlay-served reads of e32/o32). */
    const uint32_t e32_va    = band_va + AlignPage(pe.ImageSize());
    const uint32_t o32_va    = Align4(e32_va + L.size);
    const uint32_t band_used = (o32_va + uint32_t(nsec) * kO32RomSize) - band_va;
    if (band_used > band_size) {
        LOG(Caution, "%s stub needs 0x%X band bytes but the band is 0x%X - raise "
                "kInjectionBandSize in cerf_virt_addr_map.h\n",
            victim_name, band_used, band_size);
        CerfFatalExit();
    }

    std::vector<uint8_t> patched_bytes(pe.Bytes().begin(), pe.Bytes().end());
    const int32_t code_delta = int32_t(run_base) - int32_t(pe.ImageBase());
    uint32_t reloc_count = 0;
    uint32_t unhandled_relocs = 0;
    cerf::ce_image_relocator::ApplyRelocations(
        patched_bytes, pe, realaddr, code_delta, reloc_count, unhandled_relocs);
    LOG(GuestAdditions, "%s reloc code_delta=0x%08X patched=%u unhandled=%u\n",
        victim_name, uint32_t(code_delta), reloc_count, unhandled_relocs);
    if (unhandled_relocs > 0) {
        LOG(Caution, "%s has %u unhandled relocation entries (likely "
                "ARM_MOV32/THUMB_MOV32) - the stub would jump to unrelocated "
                "addresses; rebuild it without MOVW/MOVT or extend "
                "ce_image_relocator\n", victim_name, unhandled_relocs);
        CerfFatalExit();
    }

    const uint32_t e32_pa = band_pa + (e32_va - band_va);
    const uint32_t o32_pa = band_pa + (o32_va - band_va);
    WriteE32Rom(e32_pa, pe, target_vbase);
    WriteO32Array(o32_pa, pe, dataptr, realaddr, flags);
    WriteSectionBytes(sec_pa, pe, patched_bytes);

    const uint32_t entry_pa = pt.VaToPa(entry_kva);
    mem.WriteWord(entry_pa + kTocOffNFileSize,  uint32_t(pe_bytes_size));
    mem.WriteWord(entry_pa + kTocOffE32Offset,  e32_va);
    mem.WriteWord(entry_pa + kTocOffO32Offset,  o32_va);
    mem.WriteWord(entry_pa + kTocOffLoadOffset, band_va);

    LOG(GuestAdditions, "%s stub injected (%s): idx=%zu entry_kva=0x%08X "
              "band VA=0x%08X PA=0x%08X e32=0x%08X o32=0x%08X sections=%zu "
              "vbase=0x%08X run_base=0x%08X\n",
        victim_name, in_place ? "in-place" : "copy", victim_idx, entry_kva,
        band_va, band_pa, e32_va, o32_va, nsec, target_vbase, run_base);

    /* The band PA region is volatile (cerf_virt window) and a RAMIMAGE board's
       TOC-entry span is volatile - replay the band writes + the TOCentry repoint
       so a cold boot restores them. */
    auto& coldboot = emu_.Get<GuestColdBoot>();
    for (size_t i = 0; i < nsec; ++i) {
        const auto& s = pe.Sections()[i];
        if (s.psize > 0 && size_t(s.pe_file_off) + s.psize <= patched_bytes.size()) {
            coldboot.RecordPatch(sec_pa[i], s.psize);
        }
    }
    coldboot.RecordPatch(e32_pa, L.size);
    coldboot.RecordPatch(o32_pa, uint32_t(nsec) * kO32RomSize);
    coldboot.RecordPatch(entry_pa + kTocOffNFileSize,  4);
    coldboot.RecordPatch(entry_pa + kTocOffE32Offset,  4);
    coldboot.RecordPatch(entry_pa + kTocOffO32Offset,  4);
    coldboot.RecordPatch(entry_pa + kTocOffLoadOffset, 4);
    /* ComputeVbase may have lowered ROMHDR.dllfirst (DllLoadBase) for a
       relocated section-1 vbase; replay it on hard reset. */
    if (!in_place) {
        coldboot.RecordPatch(pt.VaToPa(primary_toc.romhdr_va) + kHdrDllFirstOff, 4);
    }
    return true;
}

}  /* namespace */

REGISTER_SERVICE(GuestAdditionsInjector);
