#include "cerf_debug_log.h"

#include <windows.h>
#include <pkfuncs.h>   /* VirtualCopy */

/* Guest debug-log transport: CerfInitLogging arms this process's log channel and
   CerfDebugTx / CerfDebugTxX stream characters to it. Always compiled — normal
   driver logs (CERF_LOG) reach cerf.log in production too; the per-op graphics
   tracing tier (CERF_LOG_DEV) is what production strips, at its call sites. */

#define CERF_VIRT_LOG_CH_BASE   0xD0007000u
#define CERF_VIRT_LOG_CH_STRIDE 0x1000u
#define CERF_VIRT_LOG_CH_SZ     0x1000u

/* cerf_guest's writable statics are one shared instance across every process that
   loads the module; a VirtualAlloc VA is valid only in its creating process, so a
   single shared channel pointer is read as a foreign VA by the other process and
   faults. The mapped VA is therefore keyed by owning process id. */
#define CERF_LOG_MAX_PROC 8
typedef struct { DWORD pid; volatile UCHAR* va; } CerfLogSlot;
static CerfLogSlot s_log_slot[CERF_LOG_MAX_PROC];
static LONG        s_log_slot_next = 0;

static volatile UCHAR* CerfLogTx(void) {
    DWORD pid = GetCurrentProcessId();
    int i;
    for (i = 0; i < CERF_LOG_MAX_PROC; ++i)
        if (s_log_slot[i].pid == pid) return s_log_slot[i].va;
    return NULL;
}

extern "C" void CerfInitLogging(ULONG id) {
    DWORD pid = GetCurrentProcessId();
    volatile UCHAR* va;
    ULONG pa;
    LONG idx;
    int i;
    for (i = 0; i < CERF_LOG_MAX_PROC; ++i)
        if (s_log_slot[i].pid == pid) return;   /* this process already armed */
    pa = CERF_VIRT_LOG_CH_BASE + id * CERF_VIRT_LOG_CH_STRIDE;
    va = (volatile UCHAR*)VirtualAlloc(0, CERF_VIRT_LOG_CH_SZ, MEM_RESERVE, PAGE_NOACCESS);
    if (!va) return;
    if (!VirtualCopy((LPVOID)va, (LPVOID)(pa >> 8), CERF_VIRT_LOG_CH_SZ,
                     PAGE_READWRITE | PAGE_NOCACHE | PAGE_PHYSICAL)) {
        VirtualFree((LPVOID)va, 0, MEM_RELEASE);
        return;
    }
    idx = InterlockedIncrement(&s_log_slot_next) - 1;
    if (idx < 0 || idx >= CERF_LOG_MAX_PROC) { VirtualFree((LPVOID)va, 0, MEM_RELEASE); return; }
    s_log_slot[idx].va  = va;
    s_log_slot[idx].pid = pid;   /* publish pid last: CerfLogTx keys on it */
}

extern "C" void CerfDebugTx(const char* msg) {
    volatile UCHAR* tx = CerfLogTx();
    const char* p;
    if (!tx || !msg) return;
    for (p = msg; *p; ++p) tx[0] = (UCHAR)*p;
    tx[0] = '\n';
}

extern "C" void CerfDebugTxX(const char* msg, DWORD value) {
    volatile UCHAR* tx = CerfLogTx();
    static const char hex[] = "0123456789ABCDEF";
    const char* p;
    int i;
    if (!tx || !msg) return;
    for (p = msg; *p; ++p) tx[0] = (UCHAR)*p;
    tx[0] = ' '; tx[0] = '0'; tx[0] = 'x';
    for (i = 7; i >= 0; --i) tx[0] = hex[(value >> (i * 4)) & 0xF];
    tx[0] = '\n';
}
