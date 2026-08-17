#include "ktp400_pdcfs_layout.h"

#include <algorithm>
#include <cstring>

namespace {

void Put16(uint8_t* p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8u);
}

void Put32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8u);
    p[2] = static_cast<uint8_t>(value >> 16u);
    p[3] = static_cast<uint8_t>(value >> 24u);
}

uint32_t Get32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8u) |
           (uint32_t(p[2]) << 16u) | (uint32_t(p[3]) << 24u);
}

}

namespace ktp400_emmc {

void EnsurePdcfsLayout(std::vector<uint8_t>& data,
                       const PersistRange& persist_range) {
    if (data.size() < 16u * 1024u * 1024u)
        return;

    const uint8_t* mbr = data.data();
    if (mbr[510] != 0x55u || mbr[511] != 0xAAu)
        return;
    const uint32_t part_lba = Get32(mbr + 446u + 8u);
    if (part_lba == 0u || (uint64_t(part_lba) + 1u) * 512u > data.size())
        return;

    uint8_t* bpb = data.data() + uint64_t(part_lba) * 512u;
    const uint16_t bytes_per_sec = uint16_t(bpb[11] | (uint16_t(bpb[12]) << 8));

    /* Only touch the FAT32/PDCFS volume layout used by the KTP400 eMMC image.
       If the guest has not formatted yet, synthesize the same minimal FAT32
       geometry PDCFS logs on a clean boot so early Siemens services see the
       expected persistent root directories instead of a blank flash. */
    if (bytes_per_sec == 0u) {
        std::memset(bpb, 0, 512u);
        bpb[0] = 0xEBu; bpb[1] = 0x58u; bpb[2] = 0x90u;
        std::memcpy(bpb + 3u, "MSDOS5.0", 8u);
        Put16(bpb + 11u, 512u);
        bpb[13] = 1u;                 /* one sector per cluster */
        Put16(bpb + 14u, 32u);
        bpb[16] = 2u;
        bpb[21] = 0xF8u;
        Put32(bpb + 32u, static_cast<uint32_t>((data.size() / 512u) - part_lba));
        Put32(bpb + 36u, 2032u);
        Put32(bpb + 44u, 2u);
        Put16(bpb + 48u, 1u);
        Put16(bpb + 50u, 6u);
        bpb[64] = 0x80u;
        bpb[66] = 0x29u;
        std::memcpy(bpb + 71u, "           ", 11u);
        std::memcpy(bpb + 82u, "FAT32   ", 8u);
        bpb[510] = 0x55u; bpb[511] = 0xAAu;
        persist_range(uint64_t(part_lba) * 512u, 512u);
    }

    if (uint16_t(bpb[11] | (uint16_t(bpb[12]) << 8)) != 512u ||
        bpb[13] == 0u || bpb[16] == 0u || Get32(bpb + 36u) == 0u ||
        Get32(bpb + 44u) < 2u)
        return;

    const uint32_t spc = bpb[13];
    const uint32_t reserved = uint16_t(bpb[14] | (uint16_t(bpb[15]) << 8));
    const uint32_t num_fats = bpb[16];
    const uint32_t sectors_per_fat = Get32(bpb + 36u);
    const uint32_t root_clus = Get32(bpb + 44u);
    const uint64_t fat0_off = uint64_t(part_lba + reserved) * 512u;
    const uint64_t fat1_off = uint64_t(part_lba + reserved + sectors_per_fat) * 512u;
    const uint64_t dataoff = uint64_t(part_lba + reserved + num_fats * sectors_per_fat) * 512u;
    const auto cluster_off = [&](uint32_t clus) -> uint64_t {
        return dataoff + uint64_t(clus - 2u) * spc * 512u;
    };
    const auto put_fat = [&](uint32_t clus, uint32_t value) {
        const uint64_t e0 = fat0_off + uint64_t(clus) * 4u;
        if (e0 + 4u <= data.size()) {
            Put32(data.data() + e0, value);
            persist_range(e0, 4u);
        }
        const uint64_t e1 = fat1_off + uint64_t(clus) * 4u;
        if (num_fats > 1u && e1 + 4u <= data.size()) {
            Put32(data.data() + e1, value);
            persist_range(e1, 4u);
        }
    };
    const auto get_fat = [&](uint32_t clus) -> uint32_t {
        const uint64_t e0 = fat0_off + uint64_t(clus) * 4u;
        if (e0 + 4u > data.size())
            return 0u;
        return Get32(data.data() + e0) & 0x0FFFFFFFu;
    };
    const auto write_short_entry = [&](uint8_t* ent, const char name[11], uint8_t attr, uint32_t clus) {
        std::memset(ent, 0, 32u);
        std::memcpy(ent, name, 11u);
        ent[11] = attr;
        Put16(ent + 20u, static_cast<uint16_t>(clus >> 16u));
        Put16(ent + 26u, static_cast<uint16_t>(clus & 0xFFFFu));
    };

    if (cluster_off(root_clus) + uint64_t(spc) * 512u > data.size())
        return;
    if (get_fat(0u) == 0u) put_fat(0u, 0x0FFFFFF8u);
    if (get_fat(1u) == 0u) put_fat(1u, 0x0FFFFFFFu);
    if (get_fat(root_clus) == 0u) put_fat(root_clus, 0x0FFFFFFFu);

    uint8_t* root = data.data() + cluster_off(root_clus);
    const uint32_t root_bytes = spc * 512u;
    const auto has_entry = [&](const char name[11]) {
        for (uint32_t off = 0; off + 32u <= root_bytes; off += 32u) {
            if (root[off] == 0x00u)
                return false;
            if (root[off] == 0xE5u)
                continue;
            if ((root[off + 11u] & 0x0Fu) == 0x0Fu)
                continue;
            if (std::memcmp(root + off, name, 11u) == 0)
                return true;
        }
        return false;
    };
    const auto find_short_entry = [&](const char name[11]) -> uint8_t* {
        for (uint32_t off = 0; off + 32u <= root_bytes; off += 32u) {
            if (root[off] == 0x00u)
                return nullptr;
            if (root[off] == 0xE5u)
                continue;
            if ((root[off + 11u] & 0x0Fu) == 0x0Fu)
                continue;
            if (std::memcmp(root + off, name, 11u) == 0)
                return root + off;
        }
        return nullptr;
    };
    const auto alloc_entry_run = [&](uint32_t count) -> uint8_t* {
        uint32_t run = 0;
        for (uint32_t off = 0; off + 32u <= root_bytes; off += 32u) {
            if (root[off] == 0x00u || root[off] == 0xE5u) {
                ++run;
                if (run == count)
                    return root + off + 32u - count * 32u;
            } else {
                run = 0;
            }
        }
        return nullptr;
    };
    const auto alloc_entry = [&]() -> uint8_t* { return alloc_entry_run(1u); };
    const auto alloc_cluster = [&]() -> uint32_t {
        const uint32_t total_clusters =
            static_cast<uint32_t>((data.size() - dataoff) / (uint64_t(spc) * 512u));
        for (uint32_t clus = 2u; clus < total_clusters + 2u; ++clus) {
            if (get_fat(clus) == 0u)
                return clus;
        }
        return 0u;
    };
    const auto ensure_dir = [&](const char name[11]) {
        if (has_entry(name))
            return;
        uint32_t clus = alloc_cluster();
        if (clus < 2u)
            return;
        if (cluster_off(clus) + uint64_t(spc) * 512u > data.size())
            return;
        uint8_t* ent = alloc_entry();
        if (!ent)
            return;
        put_fat(clus, 0x0FFFFFFFu);
        write_short_entry(ent, name, 0x10u, clus);
        persist_range(uint64_t(ent - data.data()), 32u);
        uint8_t* dir = data.data() + cluster_off(clus);
        std::memset(dir, 0, uint64_t(spc) * 512u);
        write_short_entry(dir, ".          ", 0x10u, clus);
        write_short_entry(dir + 32u, "..         ", 0x10u, root_clus);
        persist_range(cluster_off(clus), uint64_t(spc) * 512u);
    };
    const auto short_checksum = [](const char name[11]) -> uint8_t {
        uint8_t sum = 0;
        for (int i = 0; i < 11; ++i)
            sum = static_cast<uint8_t>(((sum & 1u) ? 0x80u : 0u) + (sum >> 1u) +
                                       static_cast<uint8_t>(name[i]));
        return sum;
    };
    const auto write_lfn_entry = [&](uint8_t* ent, const char* long_name,
                                     uint8_t checksum) {
        std::memset(ent, 0xFF, 32u);
        ent[0] = 0x41u;      /* one and final LFN slot */
        ent[11] = 0x0Fu;
        ent[12] = 0u;
        ent[13] = checksum;
        Put16(ent + 26u, 0u);
        const uint32_t dst_offsets[] = {
            1u, 3u, 5u, 7u, 9u, 14u, 16u, 18u, 20u, 22u, 24u, 28u, 30u
        };
        uint32_t i = 0;
        for (; i < 13u && long_name[i] != '\0'; ++i)
            Put16(ent + dst_offsets[i], static_cast<uint8_t>(long_name[i]));
        if (i < 13u)
            Put16(ent + dst_offsets[i++], 0u);
        for (; i < 13u; ++i)
            Put16(ent + dst_offsets[i], 0xFFFFu);
    };
    const auto cluster_from_entry = [](const uint8_t* ent) -> uint32_t {
        return (uint32_t(ent[20u] | (uint16_t(ent[21u]) << 8u)) << 16u) |
               uint32_t(ent[26u] | (uint16_t(ent[27u]) << 8u));
    };
    const auto ensure_root_file = [&](const char file_name[11]) {
        if (uint8_t* old = find_short_entry(file_name)) {
            old[11] = 0x20u;
            Put16(old + 20u, 0u);
            Put16(old + 26u, 0u);
            Put32(old + 28u, 0u);
            persist_range(uint64_t(old - data.data()), 32u);
            return;
        }
        uint8_t* ent = alloc_entry();
        if (!ent)
            return;
        write_short_entry(ent, file_name, 0x20u, 0u);
        Put32(ent + 28u, 0u);
        persist_range(uint64_t(ent - data.data()), 32u);
    };
    const auto ensure_root_file_min_size = [&](const char file_name[11],
                                               uint32_t min_size_bytes) {
        const uint32_t cluster_bytes = spc * 512u;
        if (cluster_bytes == 0u)
            return;

        uint8_t* ent = find_short_entry(file_name);
        if (!ent) {
            ent = alloc_entry();
            if (!ent)
                return;
            write_short_entry(ent, file_name, 0x20u, 0u);
        } else {
            ent[11] = 0x20u;
        }

        const uint32_t needed_clusters =
            (min_size_bytes + cluster_bytes - 1u) / cluster_bytes;

        const auto zero_existing_chain = [&](uint8_t* file_ent) -> bool {
            uint32_t clus = cluster_from_entry(file_ent);
            if (clus < 2u)
                return false;

            uint32_t remaining = min_size_bytes;
            uint32_t prev = 0u;
            for (uint32_t i = 0; i < needed_clusters; ++i) {
                if (clus < 2u || clus >= 0x0FFFFFF8u)
                    return false;
                const uint64_t off = cluster_off(clus);
                if (off + uint64_t(cluster_bytes) > data.size())
                    return false;

                const uint32_t n = std::min<uint32_t>(remaining, cluster_bytes);
                std::memset(data.data() + off, 0, n);
                if (n < cluster_bytes)
                    std::memset(data.data() + off + n, 0, cluster_bytes - n);
                persist_range(off, cluster_bytes);

                prev = clus;
                remaining = remaining > cluster_bytes ? remaining - cluster_bytes : 0u;
                if (i + 1u < needed_clusters)
                    clus = get_fat(clus);
            }

            if (prev >= 2u)
                put_fat(prev, 0x0FFFFFFFu);
            return remaining == 0u;
        };

        /* __LOG__ is a volatile PDCFS journal.  When the emulator is stopped
           without a guest clean-unmount, the persisted log may be dirty even
           though the FAT/root layout is otherwise valid; the next cold boot then
           falls back into PDCFS recovery/error paths until the whole autobacking
           image is deleted.  Real hardware completes the log lifecycle.  For the
           synthetic eMMC, always present a clean two-kilobyte log at mount time:
           keep a valid chain when possible, zero its contents, and normalize the
           directory size. */
        if (!zero_existing_chain(ent)) {
            uint32_t first = 0u;
            uint32_t prev = 0u;
            for (uint32_t i = 0; i < needed_clusters; ++i) {
                const uint32_t clus = alloc_cluster();
                if (clus < 2u)
                    return;
                if (cluster_off(clus) + uint64_t(cluster_bytes) > data.size())
                    return;
                if (first == 0u)
                    first = clus;
                if (prev != 0u)
                    put_fat(prev, clus);
                prev = clus;
                std::memset(data.data() + cluster_off(clus), 0, cluster_bytes);
                persist_range(cluster_off(clus), cluster_bytes);
                put_fat(clus, 0x0FFFFFFFu);
            }

            Put16(ent + 20u, static_cast<uint16_t>(first >> 16u));
            Put16(ent + 26u, static_cast<uint16_t>(first & 0xFFFFu));
        }

        Put32(ent + 28u, min_size_bytes);
        persist_range(uint64_t(ent - data.data()), 32u);
    };

    const auto ensure_dir_lfn = [&](const char short_name[11], const char* long_name) {
        uint32_t clus = 0;
        if (uint8_t* old = find_short_entry(short_name)) {
            const bool has_lfn =
                old >= root + 32u &&
                old[-32] == 0x41u &&
                ((old[-32 + 11] & 0x0Fu) == 0x0Fu) &&
                old[-32 + 13] == short_checksum(short_name);
            if (has_lfn)
                return;
            clus = (uint32_t(old[20u] | (uint16_t(old[21u]) << 8u)) << 16u) |
                   uint32_t(old[26u] | (uint16_t(old[27u]) << 8u));
            old[0] = 0xE5u;
            persist_range(uint64_t(old - data.data()), 32u);
        } else {
            clus = alloc_cluster();
            if (clus < 2u)
                return;
            if (cluster_off(clus) + uint64_t(spc) * 512u > data.size())
                return;
            put_fat(clus, 0x0FFFFFFFu);
            uint8_t* dir = data.data() + cluster_off(clus);
            std::memset(dir, 0, uint64_t(spc) * 512u);
            write_short_entry(dir, ".          ", 0x10u, clus);
            write_short_entry(dir + 32u, "..         ", 0x10u, root_clus);
            persist_range(cluster_off(clus), uint64_t(spc) * 512u);
        }
        uint8_t* ent = alloc_entry_run(2u);
        if (!ent)
            return;
        write_lfn_entry(ent, long_name, short_checksum(short_name));
        write_short_entry(ent + 32u, short_name, 0x10u, clus);
        persist_range(uint64_t(ent - data.data()), 64u);
    };
    const auto ensure_dir_file = [&](const char dir_short_name[11],
                                     const char file_name[11]) {
        uint8_t* dir_ent = find_short_entry(dir_short_name);
        if (!dir_ent)
            return;
        const uint32_t dir_clus =
            (uint32_t(dir_ent[20u] | (uint16_t(dir_ent[21u]) << 8u)) << 16u) |
            uint32_t(dir_ent[26u] | (uint16_t(dir_ent[27u]) << 8u));
        if (dir_clus < 2u ||
            cluster_off(dir_clus) + uint64_t(spc) * 512u > data.size())
            return;
        uint8_t* dir = data.data() + cluster_off(dir_clus);
        const uint32_t dir_bytes = spc * 512u;
        for (uint32_t off = 0; off + 32u <= dir_bytes; off += 32u) {
            if (dir[off] == 0x00u)
                break;
            if (dir[off] == 0xE5u || ((dir[off + 11u] & 0x0Fu) == 0x0Fu))
                continue;
            if (std::memcmp(dir + off, file_name, 11u) == 0)
                return;
        }
        for (uint32_t off = 0; off + 32u <= dir_bytes; off += 32u) {
            if (dir[off] != 0x00u && dir[off] != 0xE5u)
                continue;
            write_short_entry(dir + off, file_name, 0x20u, 0u);
            Put32(dir + off + 28u, 0u);
            persist_range(cluster_off(dir_clus) + off, 32u);
            return;
        }
    };
    const auto ensure_fsinfo = [&]() {
        const uint32_t total_sec32 = Get32(bpb + 32u);
        if (total_sec32 <= reserved + num_fats * sectors_per_fat || spc == 0u)
            return;
        const uint32_t total_clusters =
            (total_sec32 - reserved - num_fats * sectors_per_fat) / spc;

        uint32_t used_clusters = 0u;
        for (uint32_t clus = 2u; clus < total_clusters + 2u; ++clus) {
            if (get_fat(clus) != 0u)
                ++used_clusters;
        }
        const uint32_t free_count =
            total_clusters > used_clusters ? total_clusters - used_clusters : 0u;
        uint32_t next_free = 2u;
        for (uint32_t clus = 2u; clus < total_clusters + 2u; ++clus) {
            if (get_fat(clus) == 0u) {
                next_free = clus;
                break;
            }
        }

        const uint32_t fsinfo_sector = uint16_t(bpb[48u] | (uint16_t(bpb[49u]) << 8u));
        const uint32_t backup_sector = uint16_t(bpb[50u] | (uint16_t(bpb[51u]) << 8u));
        const auto write_fsinfo_sector = [&](uint32_t rel_sector) {
            const uint64_t off = uint64_t(part_lba + rel_sector) * 512u;
            if (rel_sector == 0u || off + 512u > data.size())
                return;
            uint8_t* fsi = data.data() + off;
            std::memset(fsi, 0, 512u);
            Put32(fsi + 0x000u, 0x41615252u);  /* FSI_LeadSig */
            Put32(fsi + 0x1E4u, 0x61417272u);  /* FSI_StrucSig */
            Put32(fsi + 0x1E8u, free_count);
            Put32(fsi + 0x1ECu, next_free);
            Put32(fsi + 0x1FCu, 0xAA550000u);  /* FSI_TrailSig */
            persist_range(off, 512u);
        };

        /* PDCFS validates the FAT32 FSInfo sector before creating its sector
           system.  Blank synthetic media had a valid BPB with FSInfo=1 but an
           all-zero sector at part_lba+1, producing:
             pdcfs_ss_validate_fsinfo, invalid FSInfo signatures
           and preventing the panel filesystem from mounting. */
        write_fsinfo_sector(fsinfo_sector);
        if (backup_sector != 0u) {
            const uint64_t backup_boot_off = uint64_t(part_lba + backup_sector) * 512u;
            if (backup_boot_off + 512u <= data.size()) {
                std::memcpy(data.data() + backup_boot_off, bpb, 512u);
                persist_range(backup_boot_off, 512u);
            }
            write_fsinfo_sector(backup_sector + fsinfo_sector);
        }
    };

    ensure_dir_lfn("DRAMST~1   ", "DRAMStore");
    ensure_dir_file("DRAMST~1   ", "DESKTOP INI");
    ensure_dir("ADDON      ");
    /* The ASW PDCFS build requires a root __LOG__ file of at least 2048 bytes.
       A zero-length preseeded file is found but rejected during
       pdcfs_log_initialize_dynamically:
         log file found but invalid length (length=0 < 2048)
       Seed a two-kilobyte zeroed log so PDCFS can mount a clean synthetic
       volume and then update the log through normal guest writes. */
    ensure_root_file_min_size("__LOG__    ", 2048u);
    ensure_fsinfo();
}

}
