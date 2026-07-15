#include "cerf_fs_driver.h"

#include <windows.h>
#include <pkfuncs.h>

#define CERF_HT_FIND              8
#define CERF_REGISTER_APISET_TYPE 0x80000000
#define CERF_FS_MAX_WATCH         32

typedef struct CerfWatch {
    HANDLE hEvent;
    HANDLE hNotify;
    BOOL   subtree;
    DWORD  filter;
    BOOL   inUse;
    WCHAR  path[CERF_FS_MAX_LFN + 1];
} CerfWatch;

static CerfWatch        g_watch[CERF_FS_MAX_WATCH];
static CRITICAL_SECTION g_notifyCs;
static HANDLE           g_hNotifyAPI  = NULL;
static BOOL             g_notifyReady = FALSE;

typedef BOOL   (*PFN_SetEventData)(HANDLE, DWORD);
typedef HANDLE (*PFN_CreateAPISet)(char*, USHORT, const PFNVOID*, const ULONGLONG*);
static PFN_SetEventData pSetEventData = NULL;

static BOOL CerfNotifyClose(CerfWatch* w) {
    if (!w) return TRUE;
    EnterCriticalSection(&g_notifyCs);
    if (w->inUse) {
        if (w->hEvent) CloseHandle(w->hEvent);
        w->hEvent = NULL; w->hNotify = NULL; w->inUse = FALSE;
    }
    LeaveCriticalSection(&g_notifyCs);
    return TRUE;
}
static BOOL CerfNotifyReset(CerfWatch* w, void* ignored) {
    (void)ignored;
    if (w && w->hEvent) ResetEvent(w->hEvent);
    return TRUE;
}

static const PFNVOID g_notifyMethods[3] = {
    (PFNVOID)CerfNotifyClose, (PFNVOID)NULL, (PFNVOID)CerfNotifyReset,
};

static const DWORD g_notifySig32[3] = { 0x000, 0x000, 0x004 };

void CerfFsNotifyInit(void) {
    OSVERSIONINFO ovi;
    HMODULE core;
    PFN_CreateAPISet pCreateAPISet;
    if (g_notifyReady) return;
    core = LoadLibraryW(L"coredll.dll");
    if (!core) return;
    pSetEventData = (PFN_SetEventData)GetProcAddressW(core, L"SetEventData");
    if (!pSetEventData) { CERF_LOG("cerf_guest: notify SetEventData absent (pre-CE5)"); return; }
    pCreateAPISet = (PFN_CreateAPISet)GetProcAddressW(core, L"CreateAPISet");
    if (!pCreateAPISet) return;

    ovi.dwOSVersionInfoSize = sizeof(ovi);
    GetVersionEx(&ovi);

    if (ovi.dwMajorVersion != 5) { CERF_LOG("cerf_guest: notify skipped (not CE5)"); return; }
    g_hNotifyAPI = pCreateAPISet("CFSN", 3, g_notifyMethods, (const ULONGLONG*)g_notifySig32);
    if (!g_hNotifyAPI) { CERF_LOG("cerf_guest: notify CreateAPISet FAILED"); return; }

    RegisterAPISet(g_hNotifyAPI, CERF_HT_FIND | CERF_REGISTER_APISET_TYPE);
    InitializeCriticalSection(&g_notifyCs);
    g_notifyReady = TRUE;
    CERF_LOG("cerf_guest: notify init complete");
}

HANDLE CerfFsFindFirstChangeNotificationW(CerfVol* vol, HANDLE hProc, PCWSTR path,
                                          BOOL subtree, DWORD filter) {
    int i, n;
    CerfWatch* w = NULL;
    HANDLE hEvent, hNotify;
    (void)vol;
    if (!g_notifyReady) { SetLastError(ERROR_NOT_SUPPORTED); return INVALID_HANDLE_VALUE; }

    EnterCriticalSection(&g_notifyCs);
    for (i = 0; i < CERF_FS_MAX_WATCH; ++i)
        if (!g_watch[i].inUse) { w = &g_watch[i]; break; }
    if (!w) {
        LeaveCriticalSection(&g_notifyCs);
        SetLastError(ERROR_TOO_MANY_OPEN_FILES);
        return INVALID_HANDLE_VALUE;
    }
    hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!hEvent) { LeaveCriticalSection(&g_notifyCs); return INVALID_HANDLE_VALUE; }

    n = path ? lstrlenW(path) : 0;
    if (n > CERF_FS_MAX_LFN) n = CERF_FS_MAX_LFN;
    if (n) memcpy(w->path, path, n * sizeof(WCHAR));
    w->path[n] = 0;
    w->hEvent = hEvent; w->subtree = subtree; w->filter = filter;
    w->hNotify = NULL;  w->inUse = TRUE;

    hNotify = CerfFsMakeHandle(g_hNotifyAPI, w, hProc);
    if (hNotify == INVALID_HANDLE_VALUE) {
        CloseHandle(hEvent); w->inUse = FALSE;
        LeaveCriticalSection(&g_notifyCs);
        return INVALID_HANDLE_VALUE;
    }
    w->hNotify = hNotify;
    pSetEventData(hEvent, (DWORD)hNotify);
    LeaveCriticalSection(&g_notifyCs);
    CERF_LOG_X("cerf_guest: FFCN watch armed hEvent", (DWORD)hEvent);
    return hEvent;
}
