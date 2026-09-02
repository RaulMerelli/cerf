#include "fwf_fsf_container.h"

#include "../../boot/fwf_oms_reader.h"
#include "../../boot/zlib_inflate.h"

#include <cstring>
#include <map>

namespace fwf_fsf {
namespace {

/* FSF header: ["FSF", NUL][4][u32 BE total record data][u32 BE record count],
   then per record:
   [9-byte descriptor][u16 BE name length][name][u32 BE size][data]. */
constexpr size_t kHeaderLen = 0x14u;
constexpr size_t kHeaderDataSumOff = 0x0Cu;
constexpr size_t kHeaderRecordCountOff = 0x10u;
constexpr size_t kDescLen = 9u;

/* Byte 6 of a record descriptor says how the data is stored: zero for the
   bytes as they are, non-zero for a zlib stream whose inflated length is
   the u32 that follows the name.  The V13 panels store plainly, the V17
   ones compress every record. */
constexpr size_t kDescStorageByte = 6u;
constexpr size_t kMaxRecordBytes = 64u * 1024u * 1024u;

constexpr uint32_t kDirEntryBytes = 32u;
constexpr uint32_t kLfnCharsPerSlot = 13u;

uint16_t Be16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8u) | p[1]);
}

uint32_t Be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24u) | (static_cast<uint32_t>(p[1]) << 16u) |
           (static_cast<uint32_t>(p[2]) << 8u) | static_cast<uint32_t>(p[3]);
}

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

/* Microsoft Extensible Firmware Initiative FAT32 File System Specification
   1.03 § 7 "Name Limits and Character Sets": the long-name checksum folds
   the eleven-byte short name one character at a time. */
uint8_t ShortChecksum(const uint8_t* short_name) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < 11u; ++i)
        sum = static_cast<uint8_t>(((sum & 1u) ? 0x80u : 0u) + (sum >> 1u) + short_name[i]);
    return sum;
}

/* FAT32 spec 1.03 § 6 "Long File Name Implementation": the alias of a long
   name is upper case, drops the characters the short namespace rejects, and
   carries a numeric tail of the form BASE~n. */
void MakeShortName(const std::string& name, uint32_t ordinal, uint8_t out[11]) {
    std::memset(out, ' ', 11u);
    const size_t dot = name.find_last_of('.');
    const std::string base = name.substr(0, dot == std::string::npos ? name.size() : dot);
    const std::string ext = dot == std::string::npos ? std::string() : name.substr(dot + 1u);

    const auto emit = [&](const std::string& src, uint32_t at, uint32_t limit) {
        uint32_t n = 0;
        for (char c : src) {
            const auto uc = static_cast<unsigned char>(c);
            const bool alnum =
                (uc >= 0x30u && uc <= 0x39u) || (uc >= 0x41u && uc <= 0x5Au) || (uc >= 0x61u && uc <= 0x7Au);
            if (!alnum) continue;
            if (n == limit) break;
            out[at + n++] = static_cast<uint8_t>(uc >= 0x61u && uc <= 0x7Au ? uc - 0x20u : uc);
        }
        return n;
    };

    uint8_t tail[6] = {0};
    uint32_t tail_len = 0;
    for (uint32_t v = ordinal; tail_len < 6u; v /= 10u) {
        tail[tail_len++] = static_cast<uint8_t>(0x30u + (v % 10u));
        if (v < 10u) break;
    }
    const uint32_t base_room = 8u - 1u - tail_len;
    uint32_t used = emit(base, 0u, base_room);
    if (used == 0u) {
        std::memcpy(out, "FILE", 4u);
        used = 4u;
    }
    out[used++] = 0x7Eu; /* '~' */
    for (uint32_t i = tail_len; i > 0u; --i)
        out[used++] = tail[i - 1u];
    emit(ext, 8u, 3u);
}

/* FAT32 spec 1.03 § 6, Table "Long Directory Entry Structure": the thirteen
   UTF-16 units of one slot sit at these byte offsets. */
void WriteLfnSlot(uint8_t* slot, const std::string& name, uint32_t index, uint32_t count, uint8_t checksum) {
    static const uint32_t kAt[kLfnCharsPerSlot] = {1u, 3u, 5u, 7u, 9u, 14u, 16u, 18u, 20u, 22u, 24u, 28u, 30u};
    std::memset(slot, 0, kDirEntryBytes);
    slot[0] = static_cast<uint8_t>((index == count ? 0x40u : 0u) | index);
    slot[11] = 0x0Fu;
    slot[13] = checksum;
    Put16(slot + 26u, 0u);
    for (uint32_t i = 0; i < kLfnCharsPerSlot; ++i) {
        const size_t pos = static_cast<size_t>(index - 1u) * kLfnCharsPerSlot + i;
        if (pos < name.size())
            Put16(slot + kAt[i], static_cast<uint8_t>(name[pos]));
        else if (pos == name.size())
            Put16(slot + kAt[i], 0u);
        else
            Put16(slot + kAt[i], 0xFFFFu);
    }
}

/* Appends 32-byte slots to a directory this seeding owns and fills strictly
   in order, extending the cluster chain when the current cluster fills. */
class DirectoryWriter {
public:
    DirectoryWriter(const FatSink& fat, uint32_t first_clus) : fat_(fat), clus_(first_clus) {
        const uint8_t* dir = fat_.cluster_ptr(clus_);
        if (!dir) {
            clus_ = 0u;
            return;
        }
        while (off_ + kDirEntryBytes <= fat_.cluster_bytes && dir[off_] != 0x00u && dir[off_] != 0xE5u)
            off_ += kDirEntryBytes;
    }

    uint8_t* Take(uint32_t slots) {
        if (clus_ == 0u || slots * kDirEntryBytes > fat_.cluster_bytes) return nullptr;
        if (off_ + slots * kDirEntryBytes > fat_.cluster_bytes) {
            /* FAT32 spec 1.03 section 6: DIR_Name[0] == 0x00 means free AND
               that no allocated entry follows, so a scan stops there.  The
               slots this request cannot use are marked 0xE5 instead, which
               means free with allocated entries still ahead. */
            uint8_t* tail = fat_.cluster_ptr(clus_);
            for (uint32_t at = off_; tail && at + kDirEntryBytes <= fat_.cluster_bytes; at += kDirEntryBytes) {
                std::memset(tail + at, 0, kDirEntryBytes);
                tail[at] = 0xE5u;
                fat_.persist(clus_, at, kDirEntryBytes);
            }
            const uint32_t next = fat_.alloc_cluster();
            uint8_t* fresh = next ? fat_.cluster_ptr(next) : nullptr;
            if (!fresh) {
                clus_ = 0u;
                return nullptr;
            }
            std::memset(fresh, 0, fat_.cluster_bytes);
            fat_.persist(next, 0u, fat_.cluster_bytes);
            fat_.link_fat(clus_, next);
            clus_ = next;
            off_ = 0u;
        }
        uint8_t* slot = fat_.cluster_ptr(clus_) + off_;
        off_ += slots * kDirEntryBytes;
        return slot;
    }

    void Persist(const uint8_t* slot, uint32_t slots) const {
        fat_.persist(clus_, static_cast<uint32_t>(slot - fat_.cluster_ptr(clus_)), slots * kDirEntryBytes);
    }

private:
    const FatSink& fat_;
    uint32_t clus_;
    uint32_t off_ = 0;
};

/* Writes the payload as a fresh cluster chain and returns its first cluster.
   An empty file gets no chain, as FAT32 spec 1.03 § 6 requires. */
uint32_t WriteChain(const FatSink& fat, const std::vector<uint8_t>& blob) {
    uint32_t first = 0u;
    uint32_t prev = 0u;
    for (size_t done = 0; done < blob.size(); done += fat.cluster_bytes) {
        const uint32_t clus = fat.alloc_cluster();
        uint8_t* dst = clus ? fat.cluster_ptr(clus) : nullptr;
        if (!dst) return first;
        const size_t chunk = blob.size() - done < fat.cluster_bytes ? blob.size() - done : fat.cluster_bytes;
        std::memcpy(dst, blob.data() + done, chunk);
        if (chunk < fat.cluster_bytes) std::memset(dst + chunk, 0, fat.cluster_bytes - chunk);
        fat.persist(clus, 0u, fat.cluster_bytes);
        if (prev)
            fat.link_fat(prev, clus);
        else
            first = clus;
        prev = clus;
    }
    return first;
}

} /* namespace */

std::vector<FsfEntry> ParseFsfVolume(const std::vector<uint8_t>& fwf_stream) {
    std::vector<uint8_t> volume;
    if (!cerf::fwf_oms::AssembleFsfVolume(fwf_stream.data(), fwf_stream.size(), volume) || volume.size() < kHeaderLen)
        return {};
    const uint32_t declared_records = Be32(volume.data() + kHeaderRecordCountOff);

    std::vector<FsfEntry> out;
    size_t off = kHeaderLen;
    while (off + kDescLen + 6u <= volume.size()) {
        const size_t p = off + kDescLen;
        const uint16_t nlen = Be16(volume.data() + p);
        if (nlen == 0u || p + 2u + nlen + 4u > volume.size()) return {};
        const std::string full(reinterpret_cast<const char*>(volume.data() + p + 2u), nlen);
        const size_t q = p + 2u + nlen;
        const uint32_t size = Be32(volume.data() + q);
        if (volume[off + kDescStorageByte] == 0u && q + 4u + size > volume.size()) return {};

        /* Record names are absolute panel paths such as
           "\flash\AddOn\mshtml.dll"; the volume root is the flash volume, so
           the leading "\flash" is dropped and the rest is the directory the
           file belongs in. */
        std::string full_path = full;
        const std::string kRoot = "\\flash\\";
        if (full_path.size() > kRoot.size() && _strnicmp(full_path.c_str(), kRoot.c_str(), kRoot.size()) == 0)
            full_path = full_path.substr(kRoot.size());
        const size_t slash = full_path.find_last_of('\\');
        FsfEntry entry;
        if (slash == std::string::npos) {
            entry.name = full_path;
        } else {
            entry.dir = full_path.substr(0, slash);
            entry.name = full_path.substr(slash + 1u);
        }

        const size_t data_at = q + 4u;
        if (volume[off + kDescStorageByte] == 0u) {
            entry.data.assign(volume.begin() + static_cast<ptrdiff_t>(data_at),
                              volume.begin() + static_cast<ptrdiff_t>(data_at + size));
            off = data_at + size;
        } else {
            if (size > kMaxRecordBytes) return {};
            size_t used = 0;
            entry.data = cerf::zlib_inflate::Decompress(volume.data() + data_at, volume.size() - data_at, size, &used);
            if (entry.data.size() != size || used == 0u) return {};
            off = data_at + used;
        }
        out.push_back(std::move(entry));
    }
    /* A compressed volume ends short of its own byte count, so the record
       count the header declares is what confirms the walk. */
    if (out.size() != declared_records) return {};
    return out;
}

namespace {

/* Compares a directory entry's name with a path component, case-insensitively
   as FAT does. */
bool NameMatches(const std::string& a, const std::string& b) {
    return a.size() == b.size() && _strnicmp(a.c_str(), b.c_str(), a.size()) == 0;
}

/* Reads the long name a directory entry carries, or its short name when the
   entry has no long-name slots ahead of it. */
std::string EntryName(const uint8_t* dir, uint32_t at) {
    std::string lfn;
    for (uint32_t k = at; k >= kDirEntryBytes; k -= kDirEntryBytes) {
        const uint8_t* e = dir + k - kDirEntryBytes;
        if (e[11] != 0x0Fu) break;
        static const uint32_t kAt[kLfnCharsPerSlot] = {1u, 3u, 5u, 7u, 9u, 14u, 16u, 18u, 20u, 22u, 24u, 28u, 30u};
        std::string part;
        for (uint32_t i = 0; i < kLfnCharsPerSlot; ++i) {
            const uint16_t ch = static_cast<uint16_t>(e[kAt[i]] | (e[kAt[i] + 1u] << 8u));
            if (ch == 0u || ch == 0xFFFFu) break;
            part.push_back(static_cast<char>(ch));
        }
        lfn = part + lfn;
        if ((e[0] & 0x40u) != 0u) break;
    }
    if (!lfn.empty()) return lfn;
    std::string sfn(reinterpret_cast<const char*>(dir + at), 11u);
    while (!sfn.empty() && sfn.back() == ' ')
        sfn.pop_back();
    return sfn;
}

} /* namespace */

uint32_t SeedFsfVolume(const std::vector<FsfEntry>& entries, uint32_t root_clus, const FatSink& fat) {
    if (root_clus < 2u || fat.cluster_bytes < 2u * kDirEntryBytes || (fat.cluster_bytes % kDirEntryBytes) != 0u)
        return 0u;

    std::map<std::string, uint32_t> dir_cache;
    std::map<uint32_t, DirectoryWriter> writers;
    uint32_t written = 0;

    /* Finds `name` among the subdirectories of `parent`, or creates it. */
    const auto find_or_create = [&](uint32_t parent, const std::string& name) -> uint32_t {
        for (uint32_t clus = parent; clus >= 2u;) {
            uint8_t* dir = fat.cluster_ptr(clus);
            if (!dir) break;
            for (uint32_t at = 0; at + kDirEntryBytes <= fat.cluster_bytes; at += kDirEntryBytes) {
                if (dir[at] == 0x00u) goto create;
                if (dir[at] == 0xE5u || (dir[at + 11u] & 0x0Fu) == 0x0Fu) continue;
                if ((dir[at + 11u] & 0x10u) == 0u) continue;
                if (NameMatches(EntryName(dir, at), name)) {
                    return (static_cast<uint32_t>(dir[at + 20u] | (dir[at + 21u] << 8u)) << 16u) |
                           static_cast<uint32_t>(dir[at + 26u] | (dir[at + 27u] << 8u));
                }
            }
            break;
        }
    create:
        const uint32_t clus = fat.alloc_cluster();
        uint8_t* body = clus ? fat.cluster_ptr(clus) : nullptr;
        if (!body) return 0u;
        std::memset(body, 0, fat.cluster_bytes);
        /* FAT32 spec 1.03 section 6: "." names the directory itself and ".."
           names the parent, with cluster 0 when that parent is the root. */
        std::memcpy(body, ".          ", 11u);
        body[11] = 0x10u;
        Put16(body + 20u, static_cast<uint16_t>(clus >> 16u));
        Put16(body + 26u, static_cast<uint16_t>(clus & 0xFFFFu));
        std::memcpy(body + kDirEntryBytes, "..         ", 11u);
        body[kDirEntryBytes + 11u] = 0x10u;
        const uint32_t up = (parent == root_clus) ? 0u : parent;
        Put16(body + kDirEntryBytes + 20u, static_cast<uint16_t>(up >> 16u));
        Put16(body + kDirEntryBytes + 26u, static_cast<uint16_t>(up & 0xFFFFu));
        fat.persist(clus, 0u, fat.cluster_bytes);

        auto it = writers.try_emplace(parent, fat, parent).first;
        const uint32_t slots = static_cast<uint32_t>((name.size() + kLfnCharsPerSlot) / kLfnCharsPerSlot);
        uint8_t* slot = it->second.Take(slots + 1u);
        if (!slot) return 0u;
        uint8_t short_name[11];
        MakeShortName(name, ++written, short_name);
        const uint8_t checksum = ShortChecksum(short_name);
        for (uint32_t i = 0; i < slots; ++i)
            WriteLfnSlot(slot + i * kDirEntryBytes, name, slots - i, slots, checksum);
        uint8_t* ent = slot + slots * kDirEntryBytes;
        std::memset(ent, 0, kDirEntryBytes);
        std::memcpy(ent, short_name, 11u);
        ent[11] = 0x10u;
        Put16(ent + 20u, static_cast<uint16_t>(clus >> 16u));
        Put16(ent + 26u, static_cast<uint16_t>(clus & 0xFFFFu));
        it->second.Persist(slot, slots + 1u);
        return clus;
    };

    const auto resolve = [&](const std::string& path) -> uint32_t {
        if (path.empty()) return root_clus;
        auto cached = dir_cache.find(path);
        if (cached != dir_cache.end()) return cached->second;
        uint32_t clus = root_clus;
        size_t at = 0;
        while (at <= path.size() && clus >= 2u) {
            const size_t sep = path.find('\\', at);
            const std::string part = path.substr(at, sep == std::string::npos ? std::string::npos : sep - at);
            if (!part.empty()) clus = find_or_create(clus, part);
            if (sep == std::string::npos) break;
            at = sep + 1u;
        }
        dir_cache[path] = clus;
        return clus;
    };

    for (const FsfEntry& entry : entries) {
        if (entry.name.empty() || entry.name.size() > 255u) continue;
        const uint32_t dir_clus = resolve(entry.dir);
        if (dir_clus < 2u) continue;

        auto it = writers.try_emplace(dir_clus, fat, dir_clus).first;
        const uint32_t lfn_slots = static_cast<uint32_t>((entry.name.size() + kLfnCharsPerSlot) / kLfnCharsPerSlot);
        uint8_t* slot = it->second.Take(lfn_slots + 1u);
        if (!slot) continue;

        uint8_t short_name[11];
        MakeShortName(entry.name, ++written, short_name);
        const uint8_t checksum = ShortChecksum(short_name);
        for (uint32_t i = 0; i < lfn_slots; ++i)
            WriteLfnSlot(slot + i * kDirEntryBytes, entry.name, lfn_slots - i, lfn_slots, checksum);

        const uint32_t first = WriteChain(fat, entry.data);
        uint8_t* ent = slot + lfn_slots * kDirEntryBytes;
        std::memset(ent, 0, kDirEntryBytes);
        std::memcpy(ent, short_name, 11u);
        ent[11] = 0x20u;
        Put16(ent + 20u, static_cast<uint16_t>(first >> 16u));
        Put16(ent + 26u, static_cast<uint16_t>(first & 0xFFFFu));
        Put32(ent + 28u, static_cast<uint32_t>(entry.data.size()));
        it->second.Persist(slot, lfn_slots + 1u);
    }
    return written;
}

} /* namespace fwf_fsf */
