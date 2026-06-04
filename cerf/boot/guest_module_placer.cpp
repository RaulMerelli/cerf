#define NOMINMAX

#include "guest_module_placer.h"

#include "rom_parser_service.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../cpu/emulated_memory.h"
#include "../boards/page_table_builder.h"

REGISTER_SERVICE(GuestModulePlacer);

namespace {

/* CE.NET/CE5 ARM section geometry (WINCE500 kernel.h: VA_SECTION=25, so a VM
   section is 1<<25 = 0x02000000). The shared ROM-DLL code region is "section
   1": IsModCodeAddr(addr) == ((addr>>25)==1), i.e. vbase in
   [0x02000000, 0x04000000) (kernel.h:794-795). */
constexpr uint32_t kModCodeBase  = 0x02000000u;
constexpr uint32_t kModCodeEnd   = 0x04000000u;
constexpr uint32_t kDllSlotAlign = 0x10000u;   /* XIP DLL 64K slot alignment */

uint32_t AlignPageP(uint32_t v) { return (v + 0xFFFu) & ~0xFFFu; }
bool IsRwPrivate(uint32_t f) { return (f & 0x80000000u) && !(f & 0x10000000u); }

}  /* namespace */

uint32_t GuestModulePlacer::ComputeVbase(uint32_t orig_vbase,
                                          uint32_t orig_slot_base,
                                          uint32_t image_size,
                                          uint32_t ce_major,
                                          uint32_t e32_off_vbase,
                                          const char* victim_name) {
    auto& parser = emu_.Get<RomParserService>();
    auto& pt  = emu_.Get<PageTableBuilder>();
    auto& mem = emu_.Get<EmulatedMemory>();
    const auto& toc = parser.Primary().xips[0].toc;

    constexpr uint32_t kO32CodeRealaddr = 16u;   /* o32[0] realaddr (romldr.h o32_rom +16) */

    uint32_t slot_ceiling = 0xFFFFFFFFu;   /* lowest module vbase > victim */
    uint32_t lowest_code  = 0xFFFFFFFFu;   /* lowest section-1 code realaddr */
    for (const auto& m : toc.modules) {
        const uint32_t e32_pa = pt.VaToPa(m.ulE32Offset);
        if (e32_pa) {
            const uint32_t vb = mem.ReadWord(e32_pa + e32_off_vbase);
            if (vb && vb > orig_vbase && vb < slot_ceiling) slot_ceiling = vb;
        }
        const uint32_t o32_pa = pt.VaToPa(m.ulO32Offset);
        if (o32_pa) {
            const uint32_t code = mem.ReadWord(o32_pa + kO32CodeRealaddr);
            if (code >= kModCodeBase && code < kModCodeEnd && code < lowest_code)
                lowest_code = code;
        }
    }
    const uint32_t slot = (slot_ceiling == 0xFFFFFFFFu)
                        ? 0xFFFFFFFFu : (slot_ceiling - orig_vbase);

    if (image_size <= slot)
        return orig_vbase;   /* fits its own slot — load in place */

    /* In-place oversized overruns the next module's section-1 VA and the kernel's
       load stomps its mapping; relocate below the lowest section-1 code (romimage
       grows dll_code_start down). Anchor on the code realaddr (CE3 has
       vbase+slot_base==codebase); new_vbase keeps slot_base. */
    if (ce_major < 3 || ce_major > 5 || lowest_code == 0xFFFFFFFFu) {
        LOG(GuestAdditions, "%s image 0x%X overflows victim slot 0x%X; relocation "
                  "N/A (ce_major=%u lowest_code=0x%08X) — in-place at 0x%08X\n",
            victim_name, image_size, slot, ce_major, lowest_code, orig_vbase);
        return orig_vbase;
    }
    const uint32_t new_code = (lowest_code - image_size) & ~(kDllSlotAlign - 1u);
    if (new_code < kModCodeBase || new_code >= lowest_code) {
        LOG(GuestAdditions, "%s image 0x%X does not fit section-1 below 0x%08X — "
                  "in-place at 0x%08X\n", victim_name, image_size, lowest_code, orig_vbase);
        return orig_vbase;
    }
    const uint32_t new_vbase = new_code - orig_slot_base;

    /* The kernel only loads ROM DLLs inside [DllLoadBase, dlllast); when the
       relocated vbase falls below DllLoadBase, grow the region down by lowering
       dllfirst's high half (DllLoadBase). Keep the low half — on a CE3 ROM it is
       0 (no RW split), so no per-process reservation is added. */
    const uint32_t romhdr_pa     = pt.VaToPa(toc.romhdr_va);
    const uint32_t dllfirst      = mem.ReadWord(romhdr_pa);
    const uint32_t dll_load_base = dllfirst & 0xFFFF0000u;
    if (new_vbase < dll_load_base) {
        const uint32_t new_dllfirst = (new_vbase & 0xFFFF0000u) | (dllfirst & 0xFFFFu);
        mem.WriteWord(romhdr_pa, new_dllfirst);
        LOG(GuestAdditions, "%s grew DLL region: DllLoadBase 0x%08X -> 0x%08X "
                  "(dllfirst 0x%08X -> 0x%08X)\n",
            victim_name, dll_load_base, new_vbase & 0xFFFF0000u, dllfirst, new_dllfirst);
    }

    LOG(GuestAdditions, "%s overflows victim slot 0x%X; relocated code 0x%08X -> "
              "0x%08X, vbase 0x%08X -> 0x%08X (below lowest section-1 code 0x%08X)\n",
        victim_name, slot, orig_vbase + orig_slot_base, new_code,
        orig_vbase, new_vbase, lowest_code);
    return new_vbase;
}

bool GuestModulePlacer::ExtendDllRwRegion(
    const std::vector<ModuleSection>& units,
    uint32_t base_vbase, uint32_t slot_base, uint32_t& out_data_base) {
    out_data_base = 0;

    uint32_t rw_total = 0;
    for (const auto& u : units)
        if (IsRwPrivate(u.flags))
            rw_total += AlignPageP(u.vsize > u.psize ? u.vsize : u.psize);
    if (rw_total == 0)
        return false;

    /* Writable data in the shared section-1 region [kModCodeBase, kModCodeEnd)
       is a single copy across processes, so a second loader (the device.exe
       carrier) would clobber gwes's copy; it needs a per-process slot-0 home.
       ASID kernels base modules high, so their data is already per-process. */
    const uint32_t natural_base = base_vbase + slot_base;
    if (natural_base < kModCodeBase || natural_base >= kModCodeEnd)
        return false;

    auto& pt  = emu_.Get<PageTableBuilder>();
    auto& mem = emu_.Get<EmulatedMemory>();
    const auto& xips = emu_.Get<RomParserService>().Primary().xips;

    /* A XIP has a per-process DLL-RW region only when dllfirst<<16 is non-zero
       and below dlllast (loader.c ReserveDllRW). CE2.11/CE3 ROMs have none and
       make section-1 DLL data per-process via the loader's ZeroPtr path; return
       false there (synthesizing a reservation breaks the load). */
    uint32_t romhdr_pa = 0, dll_load_base = 0;
    int valid = 0;
    for (size_t i = 0; i < xips.size(); ++i) {
        const auto& toc = xips[i].toc;
        const uint32_t pa       = pt.VaToPa(toc.romhdr_va);
        const uint32_t dllfirst = mem.ReadWord(pa);
        const uint32_t floor    = dllfirst << 16;
        const bool ok = (floor != 0) && (floor < toc.romhdr.dlllast);
        LOG(GuestAdditions, "ExtendDllRwRegion xip[%zu] dllfirst=0x%08X floor=0x%08X "
                  "dlllast=0x%08X valid=%d\n",
            i, dllfirst, floor, toc.romhdr.dlllast, ok);
        if (ok) {
            ++valid;
            romhdr_pa     = pa;
            dll_load_base = dllfirst & 0xFFFF0000u;
        }
    }
    if (valid == 0)
        return false;
    if (valid > 1) {
        LOG(Caution, "ExtendDllRwRegion: %d XIPs carry a DLL-RW region; need a "
                  "containment rule to pick the governing one for victim base "
                  "0x%08X\n", valid, natural_base);
        CerfFatalExit();
    }

    const auto it = extended_floors_.find(romhdr_pa);
    const uint32_t cur_floor = (it != extended_floors_.end())
                             ? it->second : (mem.ReadWord(romhdr_pa) << 16);
    const uint32_t data_base = (cur_floor - rw_total) & ~0xFFFFu;   /* 64K-aligned */
    if (data_base <= dll_load_base) {
        LOG(Caution, "ExtendDllRwRegion: injected rw 0x%X cannot extend DLL-RW floor "
                  "0x%08X below DllLoadBase 0x%08X\n",
            rw_total, cur_floor, dll_load_base);
        CerfFatalExit();
    }
    const uint32_t new_dllfirst = dll_load_base | (data_base >> 16);
    mem.WriteWord(romhdr_pa, new_dllfirst);   /* ROMHDR.dllfirst at offset 0 */
    extended_floors_[romhdr_pa] = data_base;
    out_data_base = data_base;
    LOG(GuestAdditions, "ExtendDllRwRegion floor 0x%08X -> 0x%08X (dllfirst -> 0x%08X, "
              "rw_total=0x%X, romhdr_pa=0x%08X)\n",
        cur_floor, data_base, new_dllfirst, rw_total, romhdr_pa);
    return true;
}

std::vector<uint32_t> GuestModulePlacer::ComputeSectionRealaddrs(
    const std::vector<ModuleSection>& units,
    uint32_t base_vbase, uint32_t slot_base, uint32_t data_base) {
    std::vector<uint32_t> out(units.size());
    uint32_t data_cursor = data_base;
    for (size_t i = 0; i < units.size(); ++i) {
        const auto& u = units[i];
        if (data_base != 0 && IsRwPrivate(u.flags)) {
            out[i] = data_cursor;
            data_cursor += AlignPageP(u.vsize > u.psize ? u.vsize : u.psize);
        } else {
            out[i] = base_vbase + slot_base + u.rva;
        }
    }
    return out;
}
