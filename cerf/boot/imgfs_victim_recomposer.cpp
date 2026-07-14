#define _CRT_SECURE_NO_WARNINGS

#include "imgfs_victim_recomposer.h"

#include "ce_image_relocator.h"
#include "guest_additions_binaries.h"
#include "guest_module_placer.h"
#include "pe_image.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"

#include <fstream>

REGISTER_SERVICE(ImgfsVictimRecomposer);

namespace {

constexpr uint32_t kE32ObjcntOff     = 0x00;
constexpr uint32_t kE32VbaseOff      = 0x08;
constexpr uint32_t kE32HeaderO32Base = 0x70;
constexpr uint32_t kO32Size          = 24;
constexpr uint32_t kO32RvaOff        = 4;
constexpr uint32_t kO32RealaddrOff   = 16;

/* IMGFS is WM6+, so the module header is always the CE5+ e32_rom layout. */
constexpr cerf::ce_imgfs_patcher::E32Layout kE32RomCE5plus = {
    /*size           */ 110,
    /*off_objcnt     */ 0x00,
    /*off_imageflags */ 0x02,
    /*off_entryrva   */ 0x04,
    /*off_vbase      */ 0x08,
    /*off_subsysmajor*/ 0x0C,
    /*off_subsysminor*/ 0x0E,
    /*off_stackmax   */ 0x10,
    /*off_vsize      */ 0x14,
    /*off_sect14rva  */ 0x18,
    /*off_sect14size */ 0x1C,
    /*off_timestamp  */ 0x20,
    /*off_unit       */ 0x24,
    /*off_subsys     */ 0x6C,
};

uint16_t Rd16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t Rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8)
         | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

}  // namespace

std::optional<ImgfsVictimRecomposer::Result>
ImgfsVictimRecomposer::Recompose(std::span<const uint8_t> orig_hdr,
                                 size_t                    num_sections,
                                 const std::string&        stub_path) {
    if (orig_hdr.size() < kE32HeaderO32Base) {
        LOG(Caution, "[GA recompose] victim header too short (%zu bytes)\n",
            orig_hdr.size());
        return std::nullopt;
    }
    const uint32_t orig_vbase  = Rd32(orig_hdr.data() + kE32VbaseOff);
    const uint16_t orig_objcnt = Rd16(orig_hdr.data() + kE32ObjcntOff);
    if (orig_objcnt == 0
        || size_t(kE32HeaderO32Base) + size_t(orig_objcnt) * kO32Size > orig_hdr.size()) {
        LOG(Caution, "[GA recompose] victim objcnt=%u inconsistent with header "
            "size %zu\n", orig_objcnt, orig_hdr.size());
        return std::nullopt;
    }
    const uint32_t orig_realaddr0 =
        Rd32(orig_hdr.data() + kE32HeaderO32Base + kO32RealaddrOff);
    const uint32_t orig_rva0 = Rd32(orig_hdr.data() + kE32HeaderO32Base + kO32RvaOff);
    const uint32_t slot_base = orig_realaddr0 - orig_rva0 - orig_vbase;

    std::ifstream f(stub_path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        LOG(Caution, "[GA recompose] cannot open stub %s\n", stub_path.c_str());
        CerfFatalExit();
    }
    const auto sz = f.tellg();
    std::vector<uint8_t> pe_bytes(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(pe_bytes.data()), sz);
    PeImage pe(std::move(pe_bytes));
    if (!pe.Parsed()) {
        LOG(Caution, "[GA recompose] stub PE parse failed (%s)\n", stub_path.c_str());
        CerfFatalExit();
    }

    std::vector<uint32_t> section_realaddr(pe.Sections().size());
    for (size_t i = 0; i < pe.Sections().size(); ++i)
        section_realaddr[i] = orig_vbase + slot_base + pe.Sections()[i].rva;

    std::vector<uint8_t> reloc_bytes(pe.Bytes().begin(), pe.Bytes().end());
    const int32_t code_delta = int32_t(orig_vbase) - int32_t(pe.ImageBase());
    uint32_t reloc_count = 0, unhandled = 0;
    cerf::ce_image_relocator::ApplyRelocations(
        reloc_bytes, pe, section_realaddr, code_delta, reloc_count, unhandled);
    if (unhandled > 0) {
        LOG(Caution, "[GA recompose] %u unhandled relocations in stub\n", unhandled);
        CerfFatalExit();
    }

    emu_.Get<GuestAdditionsBinaries>().StampWindowBase(reloc_bytes);

    auto slots = cerf::ce_imgfs_patcher::PackPeSections(pe, reloc_bytes, num_sections);
    if (slots.empty() || slots.size() > num_sections) {
        LOG(Caution, "[GA recompose] packing yielded %zu slots (victim sections=%zu)\n",
            slots.size(), num_sections);
        return std::nullopt;
    }
    auto& placer = emu_.Get<GuestModulePlacer>();
    for (auto& s : slots) s.flags = placer.EffSectionFlags(s.flags);

    std::vector<uint32_t> slot_realaddr;
    slot_realaddr.reserve(slots.size());
    for (const auto& s : slots) slot_realaddr.push_back(orig_vbase + slot_base + s.rva);
    auto new_hdr = cerf::ce_imgfs_patcher::BuildModuleHeader(
        kE32RomCE5plus, pe, orig_vbase, slot_realaddr, slots);

    LOG(GuestAdditions, "[GA recompose] vbase=0x%08X slots=%zu hdr=%zu reloc=%u\n",
        orig_vbase, slots.size(), new_hdr.size(), reloc_count);
    return Result{std::move(new_hdr), std::move(slots)};
}
