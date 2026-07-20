#include "cerf_demo.h"

#include <tlhelp32.h>

typedef HANDLE (WINAPI *PFN_CreateSnap)(DWORD, DWORD);
typedef BOOL   (WINAPI *PFN_Proc32)(HANDLE, LPPROCESSENTRY32);
typedef BOOL   (WINAPI *PFN_CloseSnap)(HANDLE);

int CountProcesses(void) {
    HMODULE th = LoadLibraryW(L"toolhelp.dll");
    PFN_CreateSnap create;
    PFN_Proc32 first, next;
    PFN_CloseSnap closesnap;
    HANDLE snap;
    PROCESSENTRY32 pe;
    int n = 0;
    if (!th) return 0;
    create    = (PFN_CreateSnap)GetProcAddressW(th, L"CreateToolhelp32Snapshot");
    first     = (PFN_Proc32)GetProcAddressW(th, L"Process32First");
    next      = (PFN_Proc32)GetProcAddressW(th, L"Process32Next");
    closesnap = (PFN_CloseSnap)GetProcAddressW(th, L"CloseToolhelp32Snapshot");
    if (!create || !first || !next) { FreeLibrary(th); return 0; }
    snap = create(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) { FreeLibrary(th); return 0; }
    pe.dwSize = sizeof(pe);
    if (first(snap, &pe)) {
        do { n++; pe.dwSize = sizeof(pe); } while (next(snap, &pe));
    }
    if (closesnap) closesnap(snap);
    FreeLibrary(th);
    return n;
}
