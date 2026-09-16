#pragma once

#include <cstdint>
#include <span>
#include <vector>

struct KtpMobileOatEntry {
    uint32_t va;
    uint32_t pa;
    uint32_t size;
    uint32_t flags;
};

struct KtpMobileRomOat {
    uint32_t table_va = 0;
    uint32_t magic_va = 0;
    uint32_t base_va = 0;
    std::vector<KtpMobileOatEntry> entries;

    bool valid() const { return table_va != 0 && !entries.empty(); }
};

KtpMobileRomOat FindKtpMobileOatInRom(std::span<const uint8_t> flat);

struct KtpMobileRomOalWords {
    uint32_t hw_info_slot_va = 0;
    uint32_t hw_info_cache_va = 0;

    bool valid() const { return hw_info_slot_va != 0 && hw_info_cache_va != 0; }
};

KtpMobileRomOalWords FindKtpMobileOalWordsInRom(std::span<const uint8_t> flat, uint32_t base_va);
