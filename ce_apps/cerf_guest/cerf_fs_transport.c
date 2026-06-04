#include "cerf_fs_driver.h"

#include <windows.h>
#include <pkfuncs.h>

static volatile CerfFsChannel* g_chan = NULL;

volatile CerfFsChannel* CerfFsMapChannel(void) {
    volatile CerfFsChannel* p;
    if (g_chan) return g_chan;
    p = (volatile CerfFsChannel*)VirtualAlloc(0, 0x1000, MEM_RESERVE, PAGE_NOACCESS);
    if (!p) return NULL;
    if (!VirtualCopy((LPVOID)p, (LPVOID)(CERF_FS_CHANNEL_PA >> 8), 0x1000,
                     PAGE_READWRITE | PAGE_NOCACHE | PAGE_PHYSICAL)) {
        VirtualFree((LPVOID)p, 0, MEM_RELEASE);
        return NULL;
    }
    g_chan = p;
    return g_chan;
}

/* Caller holds CerfFsLock across the whole op (field-fill + call), so the
   channel transaction needs no lock of its own. */
unsigned long CerfFsCall(CerfFsServerPB* pb, unsigned long code) {
    unsigned long result;
    volatile CerfFsChannel* ch = CerfFsMapChannel();
    if (!ch) return CERF_FS_E_GENERAL;

    ch->ServerPB = (unsigned long)pb;
    ch->Code = code;                       /* triggers the op host-side */
    while (ch->IOPending) ch->Code = CERF_FS_OP_POLL;
    result = ch->Result;

    pb->fResult = (unsigned short)result;
    return result;
}
