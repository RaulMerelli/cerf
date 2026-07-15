#include <windows.h>
#include "cerf_regs_map.h"
#include "cerf_debug_log.h"

#include "cerf/peripherals/cerf_virt/cerf_virt_addr_map.h"

#define CERF_KB_WRITE_SEQ  0x00u
#define CERF_KB_RING_BASE  0x10u
#define CERF_KB_RING_COUNT 256u
#define CERF_KB_VK_MASK    0x00FFu
#define CERF_KB_KEYUP_BIT  0x0100u

typedef VOID (WINAPI *PFN_keybd_event)(BYTE, BYTE, DWORD, DWORD);

static volatile ULONG* s_kb_regs = NULL;

static BOOL CerfMapKbRegs(void) {
    if (s_kb_regs) return TRUE;
    s_kb_regs = (volatile ULONG*)CerfMapRegsPage(g_CerfVirtBase + CerfVirt::kKeyboardOffset,
                                                 CerfVirt::kKeyboardSize);
    return s_kb_regs != NULL;
}

static DWORD WINAPI CerfKeyboardPumpThread(LPVOID) {
    HMODULE h = LoadLibraryW(L"coredll.dll");
    PFN_keybd_event ke = h ? (PFN_keybd_event)GetProcAddressW(h, L"keybd_event") : NULL;
    CERF_LOG_X("cerf_guest: kbpump keybd_event", (DWORD)ke);
    if (!ke || !CerfMapKbRegs()) {
        CERF_LOG("cerf_guest: kbpump init FAILED");
        return 0;
    }

    ULONG consumed = s_kb_regs[CERF_KB_WRITE_SEQ / 4];

    for (;;) {
        ULONG wseq = s_kb_regs[CERF_KB_WRITE_SEQ / 4];
        while (consumed != wseq) {
            if ((ULONG)(wseq - consumed) > CERF_KB_RING_COUNT)
                consumed = wseq - CERF_KB_RING_COUNT;
            ULONG idx   = consumed % CERF_KB_RING_COUNT;
            ULONG entry = s_kb_regs[(CERF_KB_RING_BASE / 4) + idx];
            BYTE  vk    = (BYTE)(entry & CERF_KB_VK_MASK);
            DWORD flags = (entry & CERF_KB_KEYUP_BIT) ? KEYEVENTF_KEYUP : 0u;
            ke(vk, 0, flags, 0);
            CERF_LOG_X("cerf_guest: kbpump inject", entry);
            consumed++;
        }
        Sleep(10);
    }
}

extern "C" void CerfStartKeyboardPump(void) {
    static BOOL started = FALSE;
    if (started) return;
    started = TRUE;
    HANDLE t = CreateThread(NULL, 0, CerfKeyboardPumpThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
