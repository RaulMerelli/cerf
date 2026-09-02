#pragma once

#include <cstdint>
#include <span>
#include <vector>

/* One OAL OEMAddressTable entry as the Siemens KTP Mobile nk.exe stores it. */
struct KtpMobileOatEntry {
    uint32_t va;
    uint32_t pa;
    uint32_t size;
    uint32_t flags;
};

struct KtpMobileRomOat {
    uint32_t table_va = 0; /* start of the entries   */
    uint32_t magic_va = 0; /* terminator/header word */
    uint32_t base_va = 0;  /* image base it declares */
    std::vector<KtpMobileOatEntry> entries;

    bool valid() const { return table_va != 0 && !entries.empty(); }
};

/* Locates the OAL OEMAddressTable inside a flat XIP image.

   The table is self-describing: it ends with a zero entry followed by the
   header word 0x87654321, and the three words after that magic are the VA of
   the table itself, zero, and the VA the image is based at.  That triple is
   what identifies the real table among the other occurrences of the magic, so
   no per-build address has to be recorded anywhere.

   Verified against the KTP400 Mobile and KTP700/900 Mobile V13 images, whose
   tables the boards previously carried as constants, and against the five V17
   Mobile Panel images, which place the table elsewhere and describe a
   different memory map. */
KtpMobileRomOat FindKtpMobileOatInRom(std::span<const uint8_t> flat);

/* The two OAL data words the MicroOMS hardware-info handoff needs. */
struct KtpMobileRomOalWords {
    uint32_t hw_info_slot_va = 0;  /* holds the handoff PA the OAL reads   */
    uint32_t hw_info_cache_va = 0; /* holds the VA the OAL caches there    */

    bool valid() const { return hw_info_slot_va != 0 && hw_info_cache_va != 0; }
};

/* Locates them by their reader, which is byte-identical across the V13 and
   V17 Mobile Panel images: it loads the cache slot, returns early when it is
   already set, then loads the handoff-PA slot and ignores the sentinel -1.
   Both addresses sit in that function's literal pool, so decoding its two
   PC-relative loads yields them without recording either per build. */
KtpMobileRomOalWords FindKtpMobileOalWordsInRom(std::span<const uint8_t> flat, uint32_t base_va);
