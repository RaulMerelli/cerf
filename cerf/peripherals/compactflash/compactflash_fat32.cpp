#include "compactflash_fat32.h"

#include "compactflash_fat_common.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

REGISTER_SERVICE(CompactFlashFat32Builder);

namespace {

constexpr uint32_t kBytesPerSec = 512;
constexpr uint32_t kSecPerClus  = 1;       /* 512-byte clusters */
constexpr uint32_t kReserved    = 32;      /* FAT32 reserved sectors */
constexpr uint32_t kNumFats     = 2;
constexpr uint32_t kRootClus    = 2;
constexpr uint32_t kMinClusters = 66000;   /* > 65525 -> valid FAT32 */
constexpr uint32_t kEoc         = 0x0FFFFFFFu;

using cf_fat::Wr16;
using cf_fat::Wr32;

}  /* namespace */

bool CompactFlashFat32Builder::Build(const std::wstring& out_path,
                                     const std::vector<std::wstring>& files,
                                     uint32_t data_mb) {
    struct Entry {
        std::wstring        name;
        std::vector<uint8_t> data;
        uint8_t             sfn[11];
        uint8_t             ntres = 0;
        uint32_t            lfn_count = 0;
        uint32_t            first_clus = 0;
        uint32_t            clusters = 0;
    };

    std::vector<Entry> entries;
    std::vector<std::array<uint8_t, 11>> used_sfns;
    auto sfn_taken = [&](const uint8_t* s) {
        for (const auto& u : used_sfns)
            if (std::memcmp(u.data(), s, 11) == 0) return true;
        return false;
    };
    uint32_t root_dir_entries = 1;          /* volume-label entry */
    uint32_t payload_clusters = 0;
    int sfn_index = 1;
    for (const auto& path : files) {
        Entry e;
        e.name = cf_fat::BaseName(path);
        e.data = cf_fat::ReadHostFile(path);
        cf_fat::ShortName sn = cf_fat::MakeShortName(e.name, sfn_index);
        if (sfn_taken(sn.sfn)) {
            cf_fat::MakeMangledSfn(e.name, sfn_index, sn.sfn);
            sn.ntres = 0;
            sn.needs_lfn = true;
        }
        ++sfn_index;
        std::memcpy(e.sfn, sn.sfn, 11);
        e.ntres = sn.ntres;
        e.lfn_count = sn.needs_lfn ? cf_fat::LfnSlotCount(e.name) : 0;
        std::array<uint8_t, 11> a{};
        std::memcpy(a.data(), sn.sfn, 11);
        used_sfns.push_back(a);
        e.clusters = static_cast<uint32_t>(
            (e.data.size() + kBytesPerSec - 1) / kBytesPerSec);
        root_dir_entries += e.lfn_count + 1;
        payload_clusters += e.clusters;
        entries.push_back(std::move(e));
    }

    const uint32_t root_clusters =
        std::max<uint32_t>(1, (root_dir_entries * 32 + 511) / 512);
    const uint32_t used = root_clusters + payload_clusters + 16;
    /* Requested capacity in clusters: data_mb MiB at kBytesPerSec*kSecPerClus
       per cluster. The card is floored to the larger of the request, the
       file payload, and the FAT32 minimum cluster count. */
    const uint32_t req_clusters = static_cast<uint32_t>(
        (static_cast<uint64_t>(data_mb) * 1024u * 1024u) /
        (kBytesPerSec * kSecPerClus));
    const uint32_t data_clusters =
        std::max(std::max(used, req_clusters), kMinClusters);
    const uint32_t fat_entries = data_clusters + 2;
    const uint32_t fat_sectors = (fat_entries * 4 + 511) / 512;
    const uint32_t total_sectors = kReserved + kNumFats * fat_sectors + data_clusters;

    std::vector<uint8_t> img(static_cast<std::size_t>(total_sectors) * 512, 0);

    /* Boot sector / BPB. */
    uint8_t* bs = img.data();
    bs[0] = 0xEB; bs[1] = 0x58; bs[2] = 0x90;
    std::memcpy(bs + 3, "MSWIN4.1", 8);
    Wr16(bs + 11, kBytesPerSec);
    bs[13] = kSecPerClus;
    Wr16(bs + 14, kReserved);
    bs[16] = kNumFats;
    Wr16(bs + 17, 0);          /* RootEntCnt = 0 (FAT32) */
    Wr16(bs + 19, 0);          /* TotSec16 = 0 */
    bs[21] = 0xF8;             /* media */
    Wr16(bs + 22, 0);          /* FATSz16 = 0 */
    Wr16(bs + 24, 63);         /* SecPerTrk */
    Wr16(bs + 26, 16);         /* NumHeads */
    Wr32(bs + 28, 0);          /* HiddSec */
    Wr32(bs + 32, total_sectors);
    Wr32(bs + 36, fat_sectors);
    Wr16(bs + 40, 0);          /* ExtFlags */
    Wr16(bs + 42, 0);          /* FSVer */
    Wr32(bs + 44, kRootClus);
    Wr16(bs + 48, 1);          /* FSInfo sector */
    Wr16(bs + 50, 6);          /* backup boot sector */
    bs[64] = 0x80;             /* drive number */
    bs[66] = 0x29;             /* boot signature */
    Wr32(bs + 67, 0xCE5FCF01u);/* volume id */
    std::memcpy(bs + 71, "CERF CF    ", 11);
    std::memcpy(bs + 82, "FAT32   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;

    /* FSInfo (sector 1) + backup boot (sector 6). */
    uint8_t* fsi = img.data() + 512;
    Wr32(fsi + 0,   0x41615252u);
    Wr32(fsi + 484, 0x61417272u);
    Wr32(fsi + 488, data_clusters - (root_clusters + payload_clusters)); /* free */
    Wr32(fsi + 492, root_clusters + payload_clusters + 2);               /* next free */
    fsi[508] = 0x00; fsi[509] = 0x00; fsi[510] = 0x55; fsi[511] = 0xAA;
    std::memcpy(img.data() + 6 * 512, bs, 512);

    /* Assign clusters: root first (clusters 2..), then each file. */
    uint32_t next = kRootClus + root_clusters;
    for (auto& e : entries) {
        e.first_clus = e.clusters ? next : 0;
        next += e.clusters;
    }

    /* FAT entries. */
    const uint32_t fat0 = kReserved * 512;
    auto set_fat = [&](uint32_t clus, uint32_t val) {
        Wr32(img.data() + fat0 + clus * 4, val);
        Wr32(img.data() + fat0 + fat_sectors * 512 + clus * 4, val);
    };
    set_fat(0, 0x0FFFFFF8u);
    set_fat(1, kEoc);
    for (uint32_t c = 0; c < root_clusters; ++c)
        set_fat(kRootClus + c, c + 1 < root_clusters ? kRootClus + c + 1 : kEoc);
    for (const auto& e : entries) {
        for (uint32_t c = 0; c < e.clusters; ++c)
            set_fat(e.first_clus + c,
                    c + 1 < e.clusters ? e.first_clus + c + 1 : kEoc);
    }

    /* Data region. */
    const uint32_t data_start = (kReserved + kNumFats * fat_sectors) * 512;
    auto clus_off = [&](uint32_t clus) {
        return data_start + (clus - kRootClus) * kSecPerClus * 512;
    };

    /* Root directory: volume-label entry, then per-file LFN run + SFN. */
    uint8_t* dir = img.data() + clus_off(kRootClus);
    std::memcpy(dir, "CERF CF    ", 11);
    dir[11] = 0x08;            /* ATTR_VOLUME_ID */
    dir += 32;
    for (const auto& e : entries)
        dir = cf_fat::EmitFileDir(dir, e.name, e.sfn, e.lfn_count, e.ntres, e.first_clus,
                                  static_cast<uint32_t>(e.data.size()));

    /* File payloads. */
    for (const auto& e : entries) {
        if (e.data.empty()) continue;
        std::memcpy(img.data() + clus_off(e.first_clus), e.data.data(), e.data.size());
    }

    std::FILE* out = nullptr;
    if (_wfopen_s(&out, out_path.c_str(), L"wb") != 0 || !out) {
        LOG(Caution, "[CF] FAT32 build: cannot open output image for write\n");
        return false;
    }
    const bool ok = std::fwrite(img.data(), 1, img.size(), out) == img.size();
    std::fclose(out);
    if (!ok) LOG(Caution, "[CF] FAT32 build: short write to image\n");
    else LOG(Cerf, "[CF] FAT32 image built: %u sectors, %zu file(s)\n",
             total_sectors, entries.size());
    return ok;
}
