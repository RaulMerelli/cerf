#pragma once

#include "ktp_mobile_f_module_model.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace ktp_mobile::detail {


constexpr std::uint8_t kPanelMarker = 0xABu;
constexpr std::uint8_t kModuleMarker = 0x5Fu;
constexpr std::uint8_t kStatusBase = 0x80u;
constexpr std::uint8_t kStartupRequestBit = 0x02u;
constexpr std::uint8_t kStartupAckBit = 0x04u;
constexpr std::size_t kOuterSequenceOffset = 0u;
constexpr std::size_t kCyclicOffset = 2u;
constexpr std::size_t kStatusOffset = 12u;
constexpr std::size_t kOuterCrcOffset = 13u;
constexpr std::size_t kRelayOffset = 15u;
constexpr std::size_t kRelayHeaderBytes = 6u;
constexpr std::size_t kRelayCrcBytes = 2u;
constexpr std::size_t kRecordHeaderBytes = 6u;
constexpr std::size_t kKnownContainerBytes = 209032u;
constexpr std::size_t kKnownVersionOffset = 208760u;
constexpr std::size_t kKnownPayloadBytes = 208908u;
constexpr std::array<std::uint8_t, 6> kUpdateTarget{{'K', 'O', 'M', 'P', '_', '2'}};
constexpr std::array<std::uint8_t, 20> kContainerIdentity{{
    'K', 'T', 'P', '_', 'M', 'O', 'B', 'I', 'L', 'E',
    '_', 'F', 'M', 'O', 'D', 'U', 'L', 'E', ' ', ' '
}};

struct CommandSchema {
    std::uint16_t command;
    std::uint32_t payload_length;
};

constexpr std::array<CommandSchema, 11> kHostReachableSchemas{{
    {201u, 205u},
    {131u, 2u},
    {132u, 7u},
    {133u, 7u},
    {134u, 1u},
    {135u, 1u},
    {138u, 20u},
    {240u, 54u},
    {239u, 240u},
    {128u, 14u},
    {130u, 2u},
}};

constexpr std::array<CommandSchema, 19> kModuleSchemas{{
    {201u, 205u},
    {131u, 4u},
    {132u, 1u},
    {133u, 7u},
    {134u, 1u},
    {135u, 2u},
    {136u, 20u},
    {137u, 4u},
    {138u, 20u},
    {242u, 10u},
    {256u, 21u},
    {257u, 4u},
    {10238u, 4u},
    {240u, 58u},
    {239u, 6u},
    {0xA000u, 6u},
    {0xA001u, 6u},
    {0xA002u, 6u},
    {0xA003u, 6u},
}};

std::uint16_t ReadBe16(const std::uint8_t* data) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[0]) << 8u) |
        static_cast<std::uint16_t>(data[1]));
}

std::uint32_t ReadBe32(const std::uint8_t* data) noexcept {
    return (static_cast<std::uint32_t>(data[0]) << 24u) |
           (static_cast<std::uint32_t>(data[1]) << 16u) |
           (static_cast<std::uint32_t>(data[2]) << 8u) |
           static_cast<std::uint32_t>(data[3]);
}

void WriteBe16(std::uint8_t* data, std::uint16_t value) noexcept {
    data[0] = static_cast<std::uint8_t>(value >> 8u);
    data[1] = static_cast<std::uint8_t>(value & 0xFFu);
}

void WriteBe32(std::uint8_t* data, std::uint32_t value) noexcept {
    data[0] = static_cast<std::uint8_t>(value >> 24u);
    data[1] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    data[2] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    data[3] = static_cast<std::uint8_t>(value & 0xFFu);
}

std::uint16_t Crc16(const std::uint8_t* data, std::size_t length) noexcept {
    std::uint16_t crc = 0u;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8u;
        for (unsigned bit = 0; bit < 8u; ++bit) {
            if ((crc & 0x8000u) != 0u) {
                crc = static_cast<std::uint16_t>((crc << 1u) ^ 0x4EABu);
            } else {
                crc = static_cast<std::uint16_t>(crc << 1u);
            }
        }
    }
    return crc;
}


std::uint32_t RotateRight(std::uint32_t value, unsigned bits) noexcept {
    return (value >> bits) | (value << (32u - bits));
}

std::array<std::uint8_t, 32> Sha256(const std::uint8_t* data,
                                    std::size_t length) noexcept {
    constexpr std::array<std::uint32_t, 64> k{{
        0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
        0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
        0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
        0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
        0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
        0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
        0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
        0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
        0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
        0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
        0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
        0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
        0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
        0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
        0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
        0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
    }};

    std::array<std::uint32_t, 8> h{{
        0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
        0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u
    }};

    const std::uint64_t bit_length = static_cast<std::uint64_t>(length) * 8u;
    const std::size_t padded_length = ((length + 9u + 63u) / 64u) * 64u;
    std::array<std::uint8_t, 64> block{};

    for (std::size_t block_offset = 0u; block_offset < padded_length;
         block_offset += 64u) {
        block.fill(0u);
        for (std::size_t i = 0u; i < 64u; ++i) {
            const std::size_t source = block_offset + i;
            if (source < length) {
                block[i] = data[source];
            } else if (source == length) {
                block[i] = 0x80u;
            }
        }
        if (block_offset + 64u == padded_length) {
            for (unsigned i = 0u; i < 8u; ++i) {
                block[56u + i] = static_cast<std::uint8_t>(
                    bit_length >> (56u - 8u * i));
            }
        }

        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0u; i < 16u; ++i) {
            const std::size_t j = i * 4u;
            w[i] = (static_cast<std::uint32_t>(block[j]) << 24u) |
                   (static_cast<std::uint32_t>(block[j + 1u]) << 16u) |
                   (static_cast<std::uint32_t>(block[j + 2u]) << 8u) |
                   static_cast<std::uint32_t>(block[j + 3u]);
        }
        for (std::size_t i = 16u; i < 64u; ++i) {
            const std::uint32_t s0 = RotateRight(w[i - 15u], 7u) ^
                                     RotateRight(w[i - 15u], 18u) ^
                                     (w[i - 15u] >> 3u);
            const std::uint32_t s1 = RotateRight(w[i - 2u], 17u) ^
                                     RotateRight(w[i - 2u], 19u) ^
                                     (w[i - 2u] >> 10u);
            w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
        }

        std::uint32_t a = h[0];
        std::uint32_t b = h[1];
        std::uint32_t c = h[2];
        std::uint32_t d = h[3];
        std::uint32_t e = h[4];
        std::uint32_t f = h[5];
        std::uint32_t g = h[6];
        std::uint32_t hh = h[7];
        for (std::size_t i = 0u; i < 64u; ++i) {
            const std::uint32_t s1 = RotateRight(e, 6u) ^ RotateRight(e, 11u) ^
                                     RotateRight(e, 25u);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = hh + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = RotateRight(a, 2u) ^ RotateRight(a, 13u) ^
                                     RotateRight(a, 22u);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    std::array<std::uint8_t, 32> digest{};
    for (std::size_t i = 0u; i < h.size(); ++i) {
        digest[4u * i] = static_cast<std::uint8_t>(h[i] >> 24u);
        digest[4u * i + 1u] = static_cast<std::uint8_t>(h[i] >> 16u);
        digest[4u * i + 2u] = static_cast<std::uint8_t>(h[i] >> 8u);
        digest[4u * i + 3u] = static_cast<std::uint8_t>(h[i]);
    }
    return digest;
}

bool ValidateContainerStructure(const std::uint8_t* data, std::size_t length) noexcept {
    if (length != kKnownContainerBytes) {
        return false;
    }
    if (!std::equal(kContainerIdentity.begin(), kContainerIdentity.end(), data)) {
        return false;
    }
    const std::uint8_t* version = data + kKnownVersionOffset;
    if (version[0] != 'V' || version[1] < '0' || version[1] > '9' ||
        version[2] != '.' || version[3] < '0' || version[3] > '9' ||
        version[4] != '.' || version[5] < '0' || version[5] > '9' ||
        std::memcmp(version + 6u, "-FModule", 8u) != 0 ||
        data[0x14u] != 'V' || data[0x15u] != version[1] - '0' ||
        data[0x16u] != version[3] - '0' || data[0x17u] != version[5] - '0') {
        return false;
    }
    const std::size_t payload = kUpdateContainerPrefixBytes;
    for (std::size_t i = 0u; i < 8u; ++i) {
        if (data[payload + i] != 0u) {
            return false;
        }
    }
    return true;
}


}  // namespace ktp_mobile::detail
