#include "arm_tlb_ops.h"

#include "arm_pte.h"

void ArmTlbFlushAll(ArmTlbUnit* unit) {
    for (size_t i = 0; i < kArmTlbSlotCount; ++i) {
        unit->slots[i].valid = false;
        /* Park the PTE field at a non-zero value so a subsequent
           pre-Valid-check read of PTEType (e.g. by a stale code
           path) doesn't see the Section-PTE encoding (0). */
        unit->slots[i].pte = ArmL2PteType::kExtendedSmallPage;
    }
    unit->next_free_slot = 0;
}

void ArmTlbInvalidateByVa(ArmTlbUnit* unit, uint32_t process_id, uint32_t va) {
    /* FCSE fold: when the low 32 MB slot is targeted, OR in the
       process ID's high 7 bits. Unconditional — cp15 c8 invalidates
       run regardless of SCTLR.M. */
    if ((va & 0xFE000000u) == 0u) {
        va |= process_id;
    }

    for (size_t i = 0; i < kArmTlbSlotCount; ++i) {
        if (!unit->slots[i].valid) continue;

        ArmL2Pte pte;
        pte.word = unit->slots[i].pte;

        uint32_t shift = 0;
        switch (pte.fault.type) {
            case 0: shift = 20; break;
            case 1: shift = 16; break;
            case 2: shift = 12; break;
            case 3: shift = 10; break;
            default: continue;
        }

        if ((va >> shift) == unit->slots[i].virtual_address) {
            unit->slots[i].valid = false;
            unit->slots[i].pte   = ArmL2PteType::kExtendedSmallPage;
            return;
        }
    }
}
