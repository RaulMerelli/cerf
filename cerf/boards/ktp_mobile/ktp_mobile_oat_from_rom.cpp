#include "ktp_mobile_oat_from_rom.h"

#include <cstring>
#include "../../core/byte_order.h"

namespace {

constexpr uint32_t kOatMagic = 0x87654321u;

constexpr size_t kMaxEntries = 64u;

}

KtpMobileRomOat FindKtpMobileOatInRom(std::span<const uint8_t> flat) {
    if (flat.size() < 32u) return {};

    for (size_t at = 0; at + 16u <= flat.size(); at += 4u) {
        if (cerf::le::U32(flat.data() + at) != kOatMagic) continue;

        const uint32_t table_va = cerf::le::U32(flat.data() + at + 4u);
        const uint32_t zero = cerf::le::U32(flat.data() + at + 8u);
        const uint32_t base_va = cerf::le::U32(flat.data() + at + 12u);
        if (zero != 0u || base_va == 0u || table_va < base_va) continue;

        const uint32_t magic_va = base_va + static_cast<uint32_t>(at);
        const size_t table_off = table_va - base_va;
        if (table_off >= at) continue;

        KtpMobileRomOat oat;
        for (size_t off = table_off; off + 16u <= at; off += 16u) {
            const KtpMobileOatEntry e{cerf::le::U32(flat.data() + off), cerf::le::U32(flat.data() + off + 4u),
                                      cerf::le::U32(flat.data() + off + 8u), cerf::le::U32(flat.data() + off + 12u)};
            if (e.va == 0u && e.size == 0u) break;
            if (e.size == 0u || oat.entries.size() >= kMaxEntries) {
                oat.entries.clear();
                break;
            }
            oat.entries.push_back(e);
        }
        if (oat.entries.empty()) continue;

        oat.table_va = table_va;
        oat.magic_va = magic_va;
        oat.base_va = base_va;
        return oat;
    }
    return {};
}

namespace {

/* ARM DDI 0406C.d A8.8.65: Thumb-2 LDR literal T1 uses Align(PC,4)+imm8*4. */
constexpr uint8_t kOalWordsReader[] = {
    0x2Du, 0xE9u, 0x18u, 0x48u, 0x0Du, 0xF2u, 0x08u, 0x0Bu, 0x07u, 0x4Cu,
    0x23u, 0x68u, 0x43u, 0xB9u, 0x05u, 0x4Bu, 0x18u, 0x68u, 0xB0u, 0xF1u,
};
constexpr size_t kCacheLdrOff = 8u;
constexpr size_t kSlotLdrOff = 14u;

size_t LdrLiteralTarget(std::span<const uint8_t> flat, size_t at) {
    const uint32_t imm8 = flat[at];
    return (((at + 4u) & ~3u)) + imm8 * 4u;
}

}

KtpMobileRomOalWords FindKtpMobileOalWordsInRom(std::span<const uint8_t> flat, uint32_t base_va) {
    const size_t n = sizeof(kOalWordsReader);
    if (flat.size() < n) return {};

    for (size_t at = 0; at + n <= flat.size(); at += 2u) {
        if (std::memcmp(flat.data() + at, kOalWordsReader, n) != 0) continue;
        const size_t cache_at = LdrLiteralTarget(flat, at + kCacheLdrOff);
        const size_t slot_at = LdrLiteralTarget(flat, at + kSlotLdrOff);
        if (cache_at + 4u > flat.size() || slot_at + 4u > flat.size()) return {};
        KtpMobileRomOalWords out;
        out.hw_info_cache_va = cerf::le::U32(flat.data() + cache_at);
        out.hw_info_slot_va = cerf::le::U32(flat.data() + slot_at);
        if (out.hw_info_slot_va < base_va || out.hw_info_cache_va < base_va) return {};
        return out;
    }
    return {};
}
