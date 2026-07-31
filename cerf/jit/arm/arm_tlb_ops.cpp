#include "arm_tlb_ops.h"

#include <cstring>

void ArmTlbFlushAll(ArmTlbUnit* unit) {
    /* tag == kArmTlbInvalidTag has low bits set, so it can never equal a
       page-aligned folded-VA tag - 0xFF-filling marks every entry empty. */
    std::memset(unit, 0xFF, sizeof(*unit));
}

void ArmTlbInvalidateByVa(ArmTlbUnit* unit, uint32_t process_id, uint32_t va) {
    /* FCSE fold for the low 32 MB slot; cp15 c8 runs regardless of SCTLR.M. */
    if ((va & 0xFE000000u) == 0u) {
        va |= process_id;
    }
    const uint32_t page = va & 0xFFFFF000u;
    const uint32_t base = ArmTlbSetBase(va);
    /* The page may sit in any way of its set - invalidate every match. Mask the
       I/O tag bit so a device-page entry for this page is cleared too. */
    for (uint32_t w = 0; w < kArmTlbWays; ++w) {
        if ((unit->entries[base + w].tag & ~kArmTlbIoTagBit) == page) {
            unit->entries[base + w].tag = kArmTlbInvalidTag;
        }
    }
}

/* Install a direct-mapped fast-path entry for a uniform RAM page. host
   corresponds to folded_va's PA, so va_addend = host - folded_va reconstructs
   the host pointer for any access in the page (the page offset cancels). */
void FillFastTlb(ArmTlbUnit* unit, uint32_t folded_va, uint8_t* host,
                 uint32_t pa, uint8_t asid, bool global, bool writable) {
    const uint32_t base = ArmTlbSetBase(folded_va);
    const uint32_t page = folded_va & 0xFFFFF000u;
    /* Reuse an existing way for the same page (e.g. a read-only entry being
       upgraded to writable) so a re-walk doesn't leave a stale duplicate;
       otherwise take a fresh way-0 slot, evicting the set's LRU way. */
    ArmTlbEntry* e = nullptr;
    for (uint32_t w = 0; w < kArmTlbWays; ++w) {
        ArmTlbEntry& c = unit->entries[base + w];
        if (c.tag == page && c.asid == asid &&
            c.global == (global ? 1u : 0u)) {
            ArmTlbPromote(unit, base, static_cast<int>(w));
            e = &unit->entries[base];
            break;
        }
    }
    if (!e) e = &ArmTlbInsertSlot(unit, base);
    e->tag       = page;
    e->va_addend = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(host) - folded_va);
    e->pa_page   = pa & 0xFFFFF000u;
    e->asid      = asid;
    e->global    = global ? 1u : 0u;
    e->writable  = writable ? 1u : 0u;
}

/* I/O analog of FillFastTlb: a device page has no host pointer, so the entry
   records its PA tagged kArmTlbIoTagBit. ArmTlbMatchIoWay later resolves it via
   SetIoPending with no walk; writable mirrors the RAM read-only-upgrade rule. */
void FillFastTlbIo(ArmTlbUnit* unit, uint32_t folded_va, uint32_t pa,
                   uint8_t asid, bool global, bool writable) {
    const uint32_t base   = ArmTlbSetBase(folded_va);
    const uint32_t io_tag = (folded_va & 0xFFFFF000u) | kArmTlbIoTagBit;
    ArmTlbEntry* e = nullptr;
    for (uint32_t w = 0; w < kArmTlbWays; ++w) {
        ArmTlbEntry& c = unit->entries[base + w];
        if (c.tag == io_tag && c.asid == asid &&
            c.global == (global ? 1u : 0u)) {
            ArmTlbPromote(unit, base, static_cast<int>(w));
            e = &unit->entries[base];
            break;
        }
    }
    if (!e) e = &ArmTlbInsertSlot(unit, base);
    e->tag       = io_tag;
    e->va_addend = 0;
    e->pa_page   = pa & 0xFFFFF000u;
    e->asid      = asid;
    e->global    = global ? 1u : 0u;
    e->writable  = writable ? 1u : 0u;
}
