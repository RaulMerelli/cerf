#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX

#include "imx51_nand_imgfs_view.h"
#include "imx51_nand_store.h"

#include "../../boot/ce_imgfs_patcher.h"
#include "../../boot/ce_imgfs_walker.h"
#include "../../boot/guest_additions_binaries.h"
#include "../../boot/imgfs_victim_recomposer.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/log.h"
#include "../../core/service.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <vector>

namespace {

constexpr uint32_t kPage              = 0x1000;
constexpr uint32_t kDirentFileSizeOff = 0x18;

uint16_t Rd16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t Rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8)
         | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

class Imx51NandGuestAdditions : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd || bd->GetRomPlacingMode() != RomPlacingMode::Imx51Nand)
            return false;
        if (!emu_.Get<DeviceConfig>().guest_additions) return false;
        return emu_.TryGet<Imx51NandStore>() != nullptr;
    }

    void OnReady() override {
        auto& view = emu_.Get<Imx51NandImgfsView>();
        if (!view.Located()) return;  /* the view logged the reason */

        const auto& victims = emu_.Get<DeviceConfig>().guest_additions_victims;
        if (victims.empty()) {
            LOG(Caution, "[NandGA] guest_additions on but no "
                "video_driver_names_for_guest_additions configured\n");
            return;
        }
        base_page_ = view.VolumeBasePage();
        const std::string stub_path = emu_.Get<GuestAdditionsBinaries>().StubPath();

        int replaced = 0;
        for (const auto& victim : victims)
            for (const auto& m : view.Modules())
                if (_stricmp(m.name.c_str(), victim.c_str()) == 0)
                    if (Inject(m, view.Volume(), stub_path)) ++replaced;
        LOG(GuestAdditions, "[NandGA] %d victim(s) replaced in NAND IMGFS\n", replaced);
    }

private:
    /* Patch `len` bytes at IMGFS logical offset `logical_off` into the NAND read
       overlay. Direct-addressed volume: logical byte L is physical NAND byte
       (base_page*kPage + L), so the overlay is keyed by (base_page + L/kPage). */
    void PatchVolume(uint64_t logical_off, const uint8_t* data, uint32_t len) {
        auto& store = emu_.Get<Imx51NandStore>();
        uint32_t done = 0;
        while (done < len) {
            const uint64_t voff  = logical_off + done;
            const uint64_t page  = base_page_ + voff / kPage;
            const uint32_t in_pg = uint32_t(voff % kPage);
            const uint32_t n     = std::min<uint32_t>(kPage - in_pg, len - done);
            std::array<uint8_t, Imx51NandStore::kMainBytes>  main{};
            std::array<uint8_t, Imx51NandStore::kSpareBytes> spare{};
            store.ReadPage(page * Imx51NandStore::kMainBytes, main.data(), spare.data());
            std::memcpy(main.data() + in_pg, data + done, n);
            store.SetReadOverlayPage(page, main.data(), spare.data());
            done += n;
        }
    }

    void WritePageBytes(uint32_t logical_page, const uint8_t* src, uint32_t real_size) {
        std::array<uint8_t, kPage> buf{};
        std::memcpy(buf.data(), src, std::min<uint32_t>(real_size, kPage));
        PatchVolume(uint64_t(logical_page) * kPage, buf.data(), kPage);
    }

    /* Logical data pages the victim's own module-header + section index blocks
       reference - freed by the replacement, reused as the stub's page pool. */
    std::vector<uint32_t> BuildFreePool(const cerf::ce_imgfs_walker::ImgfsModule& m,
                                        std::span<const uint8_t> vol,
                                        const cerf::ce_imgfs_walker::Translator& tr) {
        std::set<uint32_t> pages;
        auto collect = [&](uint32_t indexptr, uint32_t indexsize) {
            if (!indexptr || !indexsize) return;
            const std::vector<uint8_t> idx = tr.Read(vol, indexptr, indexsize);
            for (size_t o = 0; o + 8 <= idx.size(); o += 8) {
                const uint16_t comp = Rd16(idx.data() + o);
                const uint16_t full = Rd16(idx.data() + o + 2);
                const uint32_t ptr  = Rd32(idx.data() + o + 4);
                if (comp == 0 && full == 0 && ptr == 0) break;
                if (ptr == 0) continue;
                const uint32_t first = ptr / kPage;
                const uint32_t last  = (ptr + comp + kPage - 1) / kPage;
                for (uint32_t p = first; p < last; ++p) pages.insert(p);
            }
        };
        collect(m.mod_indexptr, m.mod_indexsize);
        for (const auto& s : m.sections) collect(s.sec_indexptr, s.sec_indexsize);
        return {pages.begin(), pages.end()};
    }

    bool Inject(const cerf::ce_imgfs_walker::ImgfsModule& victim,
                std::span<const uint8_t> vol, const std::string& stub_path) {
        auto tr = cerf::ce_imgfs_walker::Translator::Detect(vol, /*imgfs_base=*/0);
        const std::vector<uint8_t> orig_hdr = cerf::ce_imgfs_walker::ReadIndexData(
            vol, tr, victim.mod_indexptr, victim.mod_indexsize, victim.file_size);
        auto rc = emu_.Get<ImgfsVictimRecomposer>().Recompose(
            orig_hdr, victim.sections.size(), stub_path);
        if (!rc) return false;
        const std::vector<uint8_t>& new_hdr = rc->new_hdr;
        const auto& slots = rc->slots;

        std::vector<uint32_t> pool = BuildFreePool(victim, vol, tr);
        size_t next = 0;
        auto take = [&](uint32_t count) -> std::vector<uint32_t> {
            if (next + count > pool.size()) {
                LOG(Caution, "[NandGA] victim footprint too small: need %u, have %zu\n",
                    count, pool.size() - next);
                CerfFatalExit();
            }
            std::vector<uint32_t> out(pool.begin() + next, pool.begin() + next + count);
            next += count;
            return out;
        };

        /* Module header -> one freed page; repoint the module index block. */
        const uint32_t hdr_page = take(1)[0];
        WritePageBytes(hdr_page, new_hdr.data(), uint32_t(new_hdr.size()));
        const auto mod_idx = cerf::ce_imgfs_patcher::BuildIndexBlock(
            uint32_t(new_hdr.size()), hdr_page * kPage);
        PatchVolume(victim.mod_indexptr, mod_idx.data(), uint32_t(mod_idx.size()));
        const uint32_t hdr_size = uint32_t(new_hdr.size());
        PatchVolume(victim.dirent_off + kDirentFileSizeOff,
                    reinterpret_cast<const uint8_t*>(&hdr_size), 4);

        /* Each stub slot -> freed pages; repoint that section's index block. */
        for (size_t i = 0; i < slots.size(); ++i) {
            const auto& s = slots[i];
            const uint32_t pages_needed = (uint32_t(s.bytes.size()) + kPage - 1) / kPage;
            const std::vector<uint32_t> pg = take(pages_needed);
            std::vector<cerf::ce_imgfs_patcher::IndexRec> recs;
            recs.reserve(pages_needed);
            for (uint32_t p = 0; p < pages_needed; ++p) {
                const uint32_t off = p * kPage;
                WritePageBytes(pg[p], s.bytes.data() + off,
                               std::min<uint32_t>(kPage, uint32_t(s.bytes.size()) - off));
                recs.push_back({kPage, pg[p] * kPage});
            }
            const auto sec_idx = cerf::ce_imgfs_patcher::BuildIndexBlock(recs);
            PatchVolume(victim.sections[i].sec_indexptr, sec_idx.data(),
                        uint32_t(sec_idx.size()));
            const uint32_t sec_size = uint32_t(s.bytes.size());
            PatchVolume(victim.sections[i].dirent_off + kDirentFileSizeOff,
                        reinterpret_cast<const uint8_t*>(&sec_size), 4);
        }

        LOG(GuestAdditions, "[NandGA] '%s' replaced: slots=%zu hdr=%zu pool=%zu\n",
            victim.name.c_str(), slots.size(), new_hdr.size(), pool.size());
        return true;
    }

    uint64_t base_page_ = 0;
};

}  // namespace

REGISTER_SERVICE(Imx51NandGuestAdditions);
