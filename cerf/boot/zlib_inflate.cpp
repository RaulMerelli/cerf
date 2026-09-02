#include "zlib_inflate.h"

#include <cstring>

namespace cerf::zlib_inflate {
namespace {

/* RFC 1951 § 3.2.5, "Compressed blocks (length and distance codes)": the base
   values and extra-bit counts of the length codes 257..285. */
const uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

/* RFC 1951 § 3.2.5, the distance codes 0..29. */
const uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/* RFC 1951 § 3.2.7: the code-length alphabet is transmitted in this order. */
const uint8_t kClOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

constexpr uint32_t kMaxBits = 15u;

/* Canonical Huffman decoder built from code lengths, per RFC 1951 § 3.2.2. */
class Huffman {
public:
    bool Build(const uint8_t* lengths, uint32_t count) {
        std::memset(count_, 0, sizeof(count_));
        for (uint32_t i = 0; i < count; ++i) {
            if (lengths[i] > kMaxBits) return false;
            ++count_[lengths[i]];
        }
        count_[0] = 0;
        uint32_t left = 1;
        for (uint32_t bits = 1; bits <= kMaxBits; ++bits) {
            left <<= 1u;
            if (count_[bits] > left) return false;
            left -= count_[bits];
        }
        uint16_t offsets[kMaxBits + 2] = {0};
        for (uint32_t bits = 1; bits <= kMaxBits; ++bits)
            offsets[bits + 1] = static_cast<uint16_t>(offsets[bits] + count_[bits]);
        for (uint32_t i = 0; i < count; ++i) {
            if (lengths[i] != 0u) symbol_[offsets[lengths[i]]++] = static_cast<uint16_t>(i);
        }
        return true;
    }

    const uint16_t* counts() const { return count_; }
    const uint16_t* symbols() const { return symbol_; }

private:
    uint16_t count_[kMaxBits + 1] = {0};
    uint16_t symbol_[288] = {0};
};

class BitReader {
public:
    BitReader(const uint8_t* src, size_t size) : src_(src), size_(size) {}

    bool Bits(uint32_t need, uint32_t& out) {
        while (held_ < need) {
            if (pos_ >= size_) return false;
            value_ |= static_cast<uint32_t>(src_[pos_++]) << held_;
            held_ += 8u;
        }
        out = value_ & ((1u << need) - 1u);
        value_ >>= need;
        held_ -= need;
        return true;
    }

    /* RFC 1951 § 3.1.1: Huffman codes are packed starting with the most
       significant bit of the code, so the decoder walks one bit at a time. */
    bool Symbol(const Huffman& h, uint32_t& out) {
        uint32_t code = 0, first = 0, index = 0;
        for (uint32_t len = 1; len <= kMaxBits; ++len) {
            uint32_t bit = 0;
            if (!Bits(1u, bit)) return false;
            code |= bit;
            const uint32_t n = h.counts()[len];
            if (code - first < n) {
                out = h.symbols()[index + (code - first)];
                return true;
            }
            index += n;
            first = (first + n) << 1u;
            code <<= 1u;
        }
        return false;
    }

    /* RFC 1951 section 3.2.4: skip the remaining bits of the partially read
       byte.  Whole bytes already pulled into the buffer are not consumed yet,
       so they go back to the stream. */
    /* Bytes of the input actually consumed, excluding whole bytes still held
       in the bit buffer. */
    size_t consumed() const { return pos_ - held_ / 8u; }

    void AlignToByte() {
        pos_ -= held_ / 8u;
        value_ = 0;
        held_ = 0;
    }

    bool Take(size_t n, const uint8_t*& at) {
        if (pos_ + n > size_) return false;
        at = src_ + pos_;
        pos_ += n;
        return true;
    }

private:
    const uint8_t* src_;
    size_t size_;
    size_t pos_ = 0;
    uint32_t value_ = 0;
    uint32_t held_ = 0;
};

bool BuildFixed(Huffman& lit, Huffman& dist) {
    /* RFC 1951 § 3.2.6, the fixed literal/length and distance code lengths. */
    uint8_t lengths[288];
    for (uint32_t i = 0; i < 144u; ++i)
        lengths[i] = 8u;
    for (uint32_t i = 144u; i < 256u; ++i)
        lengths[i] = 9u;
    for (uint32_t i = 256u; i < 280u; ++i)
        lengths[i] = 7u;
    for (uint32_t i = 280u; i < 288u; ++i)
        lengths[i] = 8u;
    if (!lit.Build(lengths, 288u)) return false;
    uint8_t dist_lengths[30];
    for (uint32_t i = 0; i < 30u; ++i)
        dist_lengths[i] = 5u;
    return dist.Build(dist_lengths, 30u);
}

/* RFC 1951 § 3.2.7: the dynamic code lengths are themselves Huffman coded. */
bool BuildDynamic(BitReader& in, Huffman& lit, Huffman& dist) {
    uint32_t hlit = 0, hdist = 0, hclen = 0;
    if (!in.Bits(5u, hlit) || !in.Bits(5u, hdist) || !in.Bits(4u, hclen)) return false;
    hlit += 257u;
    hdist += 1u;
    hclen += 4u;
    if (hlit > 286u || hdist > 30u) return false;

    uint8_t cl_lengths[19] = {0};
    for (uint32_t i = 0; i < hclen; ++i) {
        uint32_t v = 0;
        if (!in.Bits(3u, v)) return false;
        cl_lengths[kClOrder[i]] = static_cast<uint8_t>(v);
    }
    Huffman cl;
    if (!cl.Build(cl_lengths, 19u)) return false;

    uint8_t lengths[288 + 30] = {0};
    uint32_t at = 0;
    while (at < hlit + hdist) {
        uint32_t sym = 0;
        if (!in.Symbol(cl, sym)) return false;
        if (sym < 16u) {
            lengths[at++] = static_cast<uint8_t>(sym);
            continue;
        }
        uint32_t repeat = 0, value = 0;
        if (sym == 16u) {
            if (at == 0u) return false;
            value = lengths[at - 1u];
            if (!in.Bits(2u, repeat)) return false;
            repeat += 3u;
        } else if (sym == 17u) {
            if (!in.Bits(3u, repeat)) return false;
            repeat += 3u;
        } else {
            if (!in.Bits(7u, repeat)) return false;
            repeat += 11u;
        }
        if (at + repeat > hlit + hdist) return false;
        for (uint32_t i = 0; i < repeat; ++i)
            lengths[at++] = static_cast<uint8_t>(value);
    }
    return lit.Build(lengths, hlit) && dist.Build(lengths + hlit, hdist);
}

bool InflateBlocks(BitReader& in, std::vector<uint8_t>& out, size_t out_size) {
    for (;;) {
        uint32_t final_block = 0, type = 0;
        if (!in.Bits(1u, final_block) || !in.Bits(2u, type)) return false;

        if (type == 0u) {
            /* RFC 1951 § 3.2.4: a stored block restarts on a byte boundary and
               carries LEN and its one's complement. */
            in.AlignToByte();
            const uint8_t* hdr = nullptr;
            if (!in.Take(4u, hdr)) return false;
            const uint32_t len = static_cast<uint32_t>(hdr[0]) | (static_cast<uint32_t>(hdr[1]) << 8u);
            const uint32_t nlen = static_cast<uint32_t>(hdr[2]) | (static_cast<uint32_t>(hdr[3]) << 8u);
            if ((len ^ 0xFFFFu) != nlen) return false;
            const uint8_t* data = nullptr;
            if (!in.Take(len, data) || out.size() + len > out_size) return false;
            out.insert(out.end(), data, data + len);
        } else if (type == 1u || type == 2u) {
            Huffman lit, dist;
            if (type == 1u ? !BuildFixed(lit, dist) : !BuildDynamic(in, lit, dist)) return false;
            for (;;) {
                uint32_t sym = 0;
                if (!in.Symbol(lit, sym)) return false;
                if (sym == 256u) break;
                if (sym < 256u) {
                    if (out.size() + 1u > out_size) return false;
                    out.push_back(static_cast<uint8_t>(sym));
                    continue;
                }
                sym -= 257u;
                if (sym >= 29u) return false;
                uint32_t extra = 0;
                if (!in.Bits(kLenExtra[sym], extra)) return false;
                const uint32_t length = kLenBase[sym] + extra;

                uint32_t dsym = 0;
                if (!in.Symbol(dist, dsym) || dsym >= 30u) return false;
                if (!in.Bits(kDistExtra[dsym], extra)) return false;
                const uint32_t distance = kDistBase[dsym] + extra;
                if (distance > out.size() || out.size() + length > out_size) return false;
                size_t from = out.size() - distance;
                for (uint32_t i = 0; i < length; ++i)
                    out.push_back(out[from + i]);
            }
        } else {
            return false;
        }
        if (final_block) return true;
    }
}

} /* namespace */

namespace {

/* RFC 1950 section 2.2: CMF/FLG, with CM==8 for deflate, the pair a multiple
   of 31, and FDICT clear for a stream carrying no preset dictionary. */
bool HasZlibHeader(const uint8_t* src, size_t src_size) {
    return src && src_size >= 2u && (src[0] & 0x0Fu) == 8u &&
           ((static_cast<uint32_t>(src[0]) << 8u) | src[1]) % 31u == 0u && (src[1] & 0x20u) == 0u;
}

} /* namespace */

std::vector<uint8_t> DecompressGrowing(const uint8_t* src, size_t src_size, size_t max_out) {
    if (!HasZlibHeader(src, src_size)) return {};
    std::vector<uint8_t> out;
    BitReader in(src + 2u, src_size - 2u);
    if (!InflateBlocks(in, out, max_out)) return {};
    return out;
}

std::vector<uint8_t> Decompress(const uint8_t* src, size_t src_size, size_t out_size, size_t* consumed) {
    /* RFC 1950 § 2.2: CMF/FLG, with CM==8 for deflate, the pair a multiple of
       31, and FDICT clear for a stream carrying no preset dictionary. */
    if (!HasZlibHeader(src, src_size)) return {};

    std::vector<uint8_t> out;
    out.reserve(out_size);
    BitReader in(src + 2u, src_size - 2u);
    if (!InflateBlocks(in, out, out_size) || out.size() != out_size) return {};
    /* RFC 1950 section 2.2: the deflate data is followed by a four-byte
       Adler-32 of the uncompressed bytes, which the caller must skip to reach
       whatever follows the stream. */
    if (consumed) *consumed = 2u + in.consumed() + 4u;
    return out;
}

} // namespace cerf::zlib_inflate
