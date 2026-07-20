#include <windows.h>
#include "cerf_regs_map.h"

#include "cerf/peripherals/cerf_virt/cerf_virt_addr_map.h"

#define CERF_PTR_X            0x00u
#define CERF_PTR_Y            0x04u
#define CERF_PTR_BUTTONS      0x08u
#define CERF_PTR_WHEEL        0x0Cu
#define CERF_PTR_SEQ          0x10u

typedef VOID (WINAPI *PFN_mouse_event)(DWORD, DWORD, DWORD, DWORD, DWORD);

static volatile ULONG* s_ptr_regs = NULL;

static BOOL CerfMapPtrRegs(void) {
    if (s_ptr_regs) return TRUE;
    s_ptr_regs = (volatile ULONG*)CerfMapRegsPage(g_CerfVirtBase + CerfVirt::kPointerOffset,
                                                  CerfVirt::kPointerSize);
    return s_ptr_regs != NULL;
}

static DWORD WINAPI CerfPointerPumpThread(LPVOID) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    HMODULE h = LoadLibraryW(L"coredll.dll");
    PFN_mouse_event me = h ? (PFN_mouse_event)GetProcAddressW(h, L"mouse_event") : NULL;
    CERF_LOG_X_DEV("cerf_guest: ptrpump mouse_event", (DWORD)me);
    if (!me || !CerfMapPtrRegs()) {
        CERF_LOG("cerf_guest: ptrpump init FAILED");
        return 0;
    }

    ULONG last_seq   = s_ptr_regs[CERF_PTR_SEQ / 4];
    ULONG last_btn   = 0;
    LONG  last_wheel = (LONG)s_ptr_regs[CERF_PTR_WHEEL / 4];

    for (;;) {
        ULONG seq = s_ptr_regs[CERF_PTR_SEQ / 4];
        if (seq != last_seq) {
            last_seq = seq;
            DWORD nx    = s_ptr_regs[CERF_PTR_X / 4];
            DWORD ny    = s_ptr_regs[CERF_PTR_Y / 4];
            ULONG btn   = s_ptr_regs[CERF_PTR_BUTTONS / 4];
            LONG  wheel = (LONG)s_ptr_regs[CERF_PTR_WHEEL / 4];

            me(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE, nx, ny, 0, 0);

            ULONG changed = btn ^ last_btn;
            if (changed & 1u) me((btn & 1u) ? MOUSEEVENTF_LEFTDOWN   : MOUSEEVENTF_LEFTUP,   0, 0, 0, 0);
            if (changed & 2u) me((btn & 2u) ? MOUSEEVENTF_RIGHTDOWN  : MOUSEEVENTF_RIGHTUP,  0, 0, 0, 0);
            if (changed & 4u) me((btn & 4u) ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP, 0, 0, 0, 0);
            last_btn = btn;

            if (wheel != last_wheel) {
                me(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)(wheel - last_wheel), 0);
                last_wheel = wheel;
            }
        }
        Sleep(10);
    }
}

extern "C" void CerfStartPointerPump(void) {
    static BOOL started = FALSE;
    if (started) return;
    started = TRUE;
    HANDLE t = CreateThread(NULL, 0, CerfPointerPumpThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
