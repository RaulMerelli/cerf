#include "cerf_dma_arena.h"
#include "cerf_regs_map.h"
#include "cerf_debug_log.h"

#include "cerf/peripherals/cerf_virt/cerf_virt_addr_map.h"

#define CERF_ARENA_NO_BASE 0xFFFFFFFFu

static volatile UCHAR*  s_arena_va = 0;
static volatile ULONG*  s_ctl      = 0;
static ULONG            s_base     = CERF_ARENA_NO_BASE;
static ULONG            s_cursor   = 0;
static CRITICAL_SECTION s_cs;

void CerfArenaProcessAttach(void) {
    InitializeCriticalSection(&s_cs);
}

static BOOL CerfArenaMap(void) {
    if (!s_arena_va)
        s_arena_va = (volatile UCHAR*)CerfMapRegsPage(
            g_CerfVirtBase + CerfVirt::kDmaArenaOffset, CerfVirt::kDmaArenaSize);
    if (!s_ctl)
        s_ctl = (volatile ULONG*)CerfMapRegsPage(
            g_CerfVirtBase + CerfVirt::kArenaCtlOffset, CerfVirt::kArenaCtlSize);
    return s_arena_va != 0 && s_ctl != 0;
}

static BOOL CerfArenaEnsure(void) {
    DWORD pid;
    ULONG i;
    if (s_base != CERF_ARENA_NO_BASE) return TRUE;
    if (!CerfArenaMap()) return FALSE;
    pid = GetCurrentProcessId();
    s_ctl[CerfVirt::kArenaCtlClaimPid / 4] = (ULONG)pid;
    for (i = 0; i < CerfVirt::kDmaArenaProcMax; ++i) {
        const ULONG off = i * CerfVirt::kDmaPartitionSize + CerfVirt::kDmaPartOwnerPid;
        if (*(volatile ULONG*)(s_arena_va + off) == (ULONG)pid) {
            s_base = i * CerfVirt::kDmaPartitionSize;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL CerfArenaEnter(void) {
    if (!CerfArenaEnsure()) return FALSE;
    EnterCriticalSection(&s_cs);
    s_cursor = CerfVirt::kDmaPartHdrSize;
    return TRUE;
}

void CerfArenaLeave(void) {
    LeaveCriticalSection(&s_cs);
}

void* CerfArenaAlloc(ULONG bytes, ULONG* out_offset) {
    const ULONG at = (s_cursor + 3u) & ~3u;
    if (bytes > CerfVirt::kDmaPartitionSize ||
        at > CerfVirt::kDmaPartitionSize - bytes) {
        CERF_LOG_X("cerf_guest: DMA partition exhausted, need", bytes);
        CERF_FATAL("cerf_guest: DMA partition exhausted - halting");
    }
    s_cursor = at + bytes;
    *out_offset = s_base + at;
    return (void*)(s_arena_va + s_base + at);
}
