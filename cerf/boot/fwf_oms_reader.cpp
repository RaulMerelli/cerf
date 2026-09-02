#include "fwf_oms_reader.h"

#include "zlib_inflate.h"

#include <algorithm>
#include <cstring>

namespace cerf::fwf_oms {
namespace {

constexpr uint8_t kStreamStart = 0x03u;
constexpr uint8_t kObjectTag = 0xA1u;
constexpr uint8_t kAttributeTag = 0xA3u;
constexpr uint8_t kLinkTag = 0xA4u;
constexpr uint32_t kNameAttrId = 233u;

constexpr uint8_t kFsfMagic[4] = {'F', 'S', 'F', 0x00u};
constexpr size_t kMaxSliceBytes = 64u * 1024u * 1024u;

constexpr uint8_t kTypeBool = 0x01u;
constexpr uint8_t kTypeUint = 0x04u;
constexpr uint8_t kTypeBlob = 0x14u;
constexpr uint8_t kTypeString = 0x15u;

uint32_t FixedWidth(uint8_t type) {
    switch (type) {
    case 0x02:
    case 0x06:
    case 0x0A: return 1u;
    case 0x03:
    case 0x07:
    case 0x0B: return 2u;
    case 0x08:
    case 0x0C:
    case 0x0E:
    case 0x12:
    case 0x13: return 4u;
    case 0x05:
    case 0x09:
    case 0x0D:
    case 0x0F:
    case 0x10:
    case 0x11: return 8u;
    default: return 0u;
    }
}

bool IsRecordTag(uint8_t b) {
    return (b >= 0xA1u && b <= 0xA7u) || b == 0xA9u;
}

bool Varint(const uint8_t* d, size_t n, size_t at, uint32_t& value, size_t& used) {
    value = 0;
    for (used = 0; at + used < n && used < 9u;) {
        const uint8_t b = d[at + used++];
        value = (value << 7u) | (b & 0x7Fu);
        if ((b & 0x80u) == 0u) return true;
    }
    return false;
}

bool ObjectHeaderLength(const uint8_t* d, size_t n, size_t at, size_t& length) {
    for (size_t q = at + 2u; q + 1u < n && q < at + 26u; ++q) {
        if (d[q] != 0x20u || d[q + 1u] != 0x00u) continue;
        for (size_t b = at + 1u; b < q; ++b) {
            if (d[b] == kObjectTag || d[b] == kAttributeTag || d[b] == kLinkTag) return false;
        }
        length = q + 2u - at;
        return true;
    }
    return false;
}

bool BlobSpan(const uint8_t* d, size_t n, size_t value_at, size_t& off, size_t& size) {
    uint32_t plain = 0;
    size_t used = 0;
    if (!Varint(d, n, value_at, plain, used)) return false;
    if (plain != 0u) {
        off = value_at + used;
        size = plain;
        return off + size <= n;
    }

    const size_t after = value_at + used;
    uint32_t streamed = 0;
    size_t used2 = 0;
    if (!Varint(d, n, after, streamed, used2) || streamed == 0u) {
        off = after;
        size = 0;
        return true;
    }
    const size_t data = after + used2;
    const size_t end = data + streamed;
    if (end <= n &&
        (end == n || d[end] == kStreamStart || IsRecordTag(d[end]) || (d[end] >= 0xB0u && d[end] <= 0xBFu))) {
        off = data;
        size = streamed;
        return true;
    }
    off = after;
    size = 0;
    return true;
}

} // namespace

std::vector<Blob> WalkBlobs(const uint8_t* src, size_t size) {
    if (!src || size < 2u || src[0] != kStreamStart) return {};

    std::vector<Blob> out;
    std::string name;
    size_t p = 1u;
    while (p < size) {
        const uint8_t tag = src[p];

        if (tag == kAttributeTag) {
            uint32_t attr_id = 0;
            size_t used = 0;
            if (!Varint(src, size, p + 1u, attr_id, used)) {
                ++p;
                continue;
            }
            const size_t q = p + 1u + used;
            if (q + 1u >= size || src[q] != 0x00u) {
                ++p;
                continue;
            }
            const uint8_t type = src[q + 1u];
            const size_t value_at = q + 2u;

            if (type == kTypeString) {
                uint32_t length = 0;
                size_t nl = 0;
                if (!Varint(src, size, value_at, length, nl) || value_at + nl + length > size) {
                    ++p;
                    continue;
                }
                if (attr_id == kNameAttrId) name.assign(reinterpret_cast<const char*>(src + value_at + nl), length);
                p = value_at + nl + length;
                continue;
            }
            if (type == kTypeBlob) {
                size_t off = 0, blob_size = 0;
                if (!BlobSpan(src, size, value_at, off, blob_size)) {
                    ++p;
                    continue;
                }
                out.push_back(Blob{attr_id, name, off, blob_size});
                p = off + blob_size;
                continue;
            }
            if (type == kTypeUint) {
                uint32_t value = 0;
                size_t nv = 0;
                if (!Varint(src, size, value_at, value, nv)) {
                    ++p;
                    continue;
                }
                p = value_at + nv;
                continue;
            }
            if (type == kTypeBool) {
                p = value_at + 1u;
                continue;
            }
            if (const uint32_t width = FixedWidth(type)) {
                p = value_at + width;
                continue;
            }
            ++p;
            continue;
        }

        if (tag == kLinkTag) {
            uint32_t attr_id = 0;
            size_t used = 0;
            if (Varint(src, size, p + 1u, attr_id, used) && p + 1u + used + 3u < size && src[p + 1u + used] == 0x10u &&
                src[p + 2u + used] == 0x00u && src[p + 3u + used] == 0x00u) {
                p = p + 1u + used + 4u;
                continue;
            }
            ++p;
            continue;
        }

        if (tag == kObjectTag) {
            size_t header = 0;
            p += ObjectHeaderLength(src, size, p, header) ? header : 1u;
            continue;
        }

        ++p;
    }
    return out;
}

bool IsOmsStream(const uint8_t* src, size_t size) {
    return src && size >= 2u && src[0] == kStreamStart && src[1] == kObjectTag;
}

bool AssembleOsImage(const uint8_t* src, size_t size, std::vector<uint8_t>& out, size_t* slice_count) {
    if (!IsOmsStream(src, size)) return false;
    out.clear();
    size_t slices = 0;
    for (const Blob& blob : WalkBlobs(src, size)) {
        const uint8_t* p = src + blob.off;
        /* RFC 1950 section 2.2: CM==8 and the two header bytes a multiple of
           31 mark the deflate streams that carry the OS image slices. */
        if (blob.size < 2u || (p[0] & 0x0Fu) != 8u || ((static_cast<uint32_t>(p[0]) << 8u) | p[1]) % 31u != 0u)
            continue;
        const std::vector<uint8_t> slice = zlib_inflate::DecompressGrowing(p, blob.size, kMaxSliceBytes);
        if (slice.empty()) return false;
        out.insert(out.end(), slice.begin(), slice.end());
        ++slices;
    }
    if (slice_count) *slice_count = slices;
    return !out.empty();
}

bool ExtractPersistentStream(const uint8_t* src, size_t size, std::vector<uint8_t>& out) {
    if (!IsOmsStream(src, size)) return false;
    for (const Blob& blob : WalkBlobs(src, size)) {
        if (blob.attr_id != 18797u || blob.name != "PersistentStream" || !IsOmsStream(src + blob.off, blob.size))
            continue;
        out.assign(src + blob.off, src + blob.off + blob.size);
        return true;
    }
    return false;
}

bool ExtractInstalledFirmwareSummary(const uint8_t* src, size_t size, std::vector<uint8_t>& out) {
    std::vector<uint8_t> persistent;
    if (!ExtractPersistentStream(src, size, persistent) || persistent.size() < 3u) return false;

    size_t root_header = 0;
    if (persistent[0] != kStreamStart || persistent[1] != kObjectTag ||
        !ObjectHeaderLength(persistent.data(), persistent.size(), 1u, root_header))
        return false;

    const auto skip_attribute = [&](size_t at, size_t& next) -> bool {
        uint32_t attr_id = 0;
        size_t id_len = 0;
        if (!Varint(persistent.data(), persistent.size(), at + 1u, attr_id, id_len)) return false;
        const size_t q = at + 1u + id_len;
        if (q + 1u >= persistent.size() || persistent[q] != 0x00u) return false;
        const uint8_t type = persistent[q + 1u];
        const size_t value_at = q + 2u;
        if (type == kTypeString) {
            uint32_t length = 0;
            size_t used = 0;
            if (!Varint(persistent.data(), persistent.size(), value_at, length, used) ||
                value_at + used + length > persistent.size())
                return false;
            next = value_at + used + length;
            return true;
        }
        if (type == kTypeBlob) {
            size_t blob_off = 0, blob_size = 0;
            if (!BlobSpan(persistent.data(), persistent.size(), value_at, blob_off, blob_size)) return false;
            next = blob_off + blob_size;
            return true;
        }
        if (type == kTypeUint) {
            uint32_t value = 0;
            size_t used = 0;
            if (!Varint(persistent.data(), persistent.size(), value_at, value, used)) return false;
            next = value_at + used;
            return true;
        }
        if (type == kTypeBool) {
            next = value_at + 1u;
            return next <= persistent.size();
        }
        const uint32_t width = FixedWidth(type);
        if (width == 0u || value_at + width > persistent.size()) return false;
        next = value_at + width;
        return true;
    };

    /* PersistentStream's first child is Firmware/Version.  Retain exactly that
       child and close the root before ImageContainer/PolymorphContainer. */
    size_t p = 1u + root_header;
    size_t version_begin = 0;
    size_t version_end = 0;
    uint32_t depth = 1u;
    while (p < persistent.size()) {
        const uint8_t tag = persistent[p];
        if (tag == kAttributeTag) {
            size_t next = 0;
            if (!skip_attribute(p, next)) return false;
            p = next;
            continue;
        }
        if (tag == kObjectTag) {
            size_t header = 0;
            if (!ObjectHeaderLength(persistent.data(), persistent.size(), p, header)) return false;
            if (depth == 1u && version_begin == 0u) version_begin = p;
            ++depth;
            p += header;
            continue;
        }
        if (tag == 0xA2u) {
            if (depth == 0u) return false;
            --depth;
            ++p;
            if (version_begin != 0u && depth == 1u) {
                version_end = p;
                break;
            }
            continue;
        }
        ++p;
    }
    if (version_begin == 0u || version_end == 0u) return false;

    static constexpr char kVersion[] = "Version";
    if (std::search(persistent.begin() + version_begin, persistent.begin() + version_end, std::begin(kVersion),
                    std::end(kVersion) - 1u) == persistent.begin() + version_end)
        return false;

    out.assign(persistent.begin(), persistent.begin() + version_end);
    out.push_back(0xA2u); /* Firmware root. */
    return true;
}

bool AssembleFsfVolume(const uint8_t* src, size_t size, std::vector<uint8_t>& out) {
    if (!IsOmsStream(src, size)) return false;
    out.clear();
    for (const Blob& blob : WalkBlobs(src, size)) {
        const uint8_t* p = src + blob.off;
        if (out.empty()) {
            if (blob.size < sizeof(kFsfMagic) || std::memcmp(p, kFsfMagic, sizeof(kFsfMagic)) != 0) continue;
        } else if (blob.size == 0u) {
            continue;
        }
        out.insert(out.end(), p, p + blob.size);
    }
    return !out.empty();
}

} // namespace cerf::fwf_oms
