#include <windows.h>
#include <tlhelp32.h>

#include "cerf_regs_map.h"

/* Register offsets below MUST match cerf/peripherals/cerf_virt/cerf_virt_task_manager_regs.h. */
#include "cerf/peripherals/cerf_virt/cerf_virt_addr_map.h"

#define CERF_TM_CMD_GEN       0x00u
#define CERF_TM_CMD_CODE      0x04u
#define CERF_TM_CMD_PID       0x08u
#define CERF_TM_CMD_RUNLEN    0x0Cu
#define CERF_TM_CMD_RUNTEXT   0x100u

#define CERF_TM_RESP_CMDGEN   0x40u
#define CERF_TM_RESP_STATUS   0x44u
#define CERF_TM_RESP_ERR      0x48u
#define CERF_TM_RESP_COUNT    0x50u
#define CERF_TM_RESP_TOTAL    0x54u
#define CERF_TM_REC_INDEX     0x60u
#define CERF_TM_REC_KICK      0x64u
#define CERF_TM_RESP_KICK     0x80u
#define CERF_TM_REC_DATA      0x200u

#define CERF_TM_OP_LIST         1u
#define CERF_TM_OP_KILL         2u
#define CERF_TM_OP_SWITCHTO     3u
#define CERF_TM_OP_RUN          4u
#define CERF_TM_OP_LISTWINDOWS  5u
#define CERF_TM_OP_SWITCHTOWIN  6u

#define CERF_TM_RUN_MAX         256u
#define CERF_TM_MAX_RECORDS     256u
#define CERF_TM_NAME_WCHARS     64u
#define CERF_TM_WIN_TITLE_WCHARS 64u
#define CERF_TM_WINFLAG_VISIBLE 0x1u

typedef struct CerfTmProcRecord {
    DWORD pid;
    DWORD parent_pid;
    DWORD thread_count;
    LONG  base_priority;
    DWORD mem_base;
    WCHAR name[CERF_TM_NAME_WCHARS];
} CerfTmProcRecord;
typedef char cerf_tm_record_size_check[(sizeof(CerfTmProcRecord) == 148) ? 1 : -1];

typedef struct CerfTmWindowRecord {
    DWORD hwnd;
    DWORD pid;
    DWORD thread_id;
    DWORD flags;
    WCHAR title[CERF_TM_WIN_TITLE_WCHARS];
} CerfTmWindowRecord;
typedef char cerf_tm_winrec_size_check[(sizeof(CerfTmWindowRecord) == 144) ? 1 : -1];

/* Windows CE forbids user APIs before the system has fully booted, and pump
   start is display-driver init (mid-boot). Toolhelp therefore resolves on the
   first command, which can only arrive after boot. */
typedef HANDLE (WINAPI *PFN_CreateToolhelp32Snapshot)(DWORD, DWORD);
typedef BOOL   (WINAPI *PFN_CloseToolhelp32Snapshot)(HANDLE);
typedef BOOL   (WINAPI *PFN_Process32First)(HANDLE, LPPROCESSENTRY32);
typedef BOOL   (WINAPI *PFN_Process32Next)(HANDLE, LPPROCESSENTRY32);

static volatile ULONG* s_tm_regs = NULL;

static BOOL s_toolhelp_resolved = FALSE;
static PFN_CreateToolhelp32Snapshot s_pfnCreateSnap = NULL;
static PFN_CloseToolhelp32Snapshot  s_pfnCloseSnap  = NULL;
static PFN_Process32First           s_pfnProcFirst  = NULL;
static PFN_Process32Next            s_pfnProcNext   = NULL;

static void CerfTmResolveToolhelp(void) {
    HMODULE h;
    if (s_toolhelp_resolved) return;
    s_toolhelp_resolved = TRUE;
    h = LoadLibraryW(L"toolhelp.dll");
    if (!h) {
        CERF_LOG("cerf_guest: tmpump toolhelp.dll missing - LIST disabled");
        return;
    }
    s_pfnCreateSnap = (PFN_CreateToolhelp32Snapshot)
        GetProcAddressW(h, L"CreateToolhelp32Snapshot");
    s_pfnCloseSnap = (PFN_CloseToolhelp32Snapshot)
        GetProcAddressW(h, L"CloseToolhelp32Snapshot");
    s_pfnProcFirst = (PFN_Process32First)GetProcAddressW(h, L"Process32First");
    s_pfnProcNext  = (PFN_Process32Next)GetProcAddressW(h, L"Process32Next");
    if (!s_pfnCreateSnap || !s_pfnCloseSnap || !s_pfnProcFirst || !s_pfnProcNext) {
        CERF_LOG("cerf_guest: tmpump toolhelp exports incomplete - LIST disabled");
        s_pfnCreateSnap = NULL;
    }
}

static void CerfTmRespond(DWORD gen, DWORD status, DWORD err,
                          DWORD count, DWORD total) {
    s_tm_regs[CERF_TM_RESP_CMDGEN / 4] = gen;
    s_tm_regs[CERF_TM_RESP_STATUS / 4] = status;
    s_tm_regs[CERF_TM_RESP_ERR / 4]    = err;
    s_tm_regs[CERF_TM_RESP_COUNT / 4]  = count;
    s_tm_regs[CERF_TM_RESP_TOTAL / 4]  = total;
    s_tm_regs[CERF_TM_RESP_KICK / 4]   = 1;   /* host consumes synchronously */
}

/* Stream one staged record (words raw words) through the MMIO window. */
static void CerfTmSendRecord(const ULONG* words, DWORD count, DWORD index) {
    DWORD i;
    for (i = 0; i < count; ++i)
        s_tm_regs[(CERF_TM_REC_DATA + i * 4) / 4] = words[i];
    s_tm_regs[CERF_TM_REC_INDEX / 4] = index;
    s_tm_regs[CERF_TM_REC_KICK / 4]  = 1;
}

static void CerfTmDoList(DWORD gen) {
    PROCESSENTRY32 pe;
    CerfTmProcRecord rec;
    HANDLE snap;
    DWORD count = 0, total = 0;
    BOOL ok;

    CerfTmResolveToolhelp();
    if (!s_pfnCreateSnap) {
        CerfTmRespond(gen, 0, ERROR_NOT_SUPPORTED, 0, 0);
        return;
    }
    snap = s_pfnCreateSnap(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        CerfTmRespond(gen, 0, GetLastError(), 0, 0);
        return;
    }

    pe.dwSize = sizeof(pe);
    ok = s_pfnProcFirst(snap, &pe);
    while (ok) {
        total++;
        if (count < CERF_TM_MAX_RECORDS) {
            DWORD i;
            rec.pid           = pe.th32ProcessID;
            rec.parent_pid    = pe.th32ParentProcessID;
            rec.thread_count  = pe.cntThreads;
            rec.base_priority = pe.pcPriClassBase;
            rec.mem_base      = pe.th32MemoryBase;
            for (i = 0; i < CERF_TM_NAME_WCHARS - 1 && pe.szExeFile[i]; ++i)
                rec.name[i] = pe.szExeFile[i];
            for (; i < CERF_TM_NAME_WCHARS; ++i)
                rec.name[i] = 0;
            CerfTmSendRecord((const ULONG*)&rec, sizeof(rec) / 4, count);
            count++;
        }
        pe.dwSize = sizeof(pe);
        ok = s_pfnProcNext(snap, &pe);
    }
    s_pfnCloseSnap(snap);
    CerfTmRespond(gen, 1, 0, count, total);
}

static void CerfTmDoKill(DWORD gen, DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    BOOL ok;
    DWORD err;
    if (!h) {
        CerfTmRespond(gen, 0, GetLastError(), 0, 0);
        return;
    }
    ok  = TerminateProcess(h, 0);
    err = ok ? 0 : GetLastError();
    CloseHandle(h);
    CerfTmRespond(gen, ok ? 1u : 0u, err, 0, 0);
}

/* Top-level walk via GetWindow: EnumWindows needs gwes to call back through a
   caller-supplied function pointer, and that callback marshaling rejects a
   kernel-space caller (cerf_guest is kernel-loaded on CE6+) with
   ERROR_INVALID_PARAMETER before enumerating anything. */
static void CerfTmDoSwitchTo(DWORD gen, DWORD pid) {
    HWND visible = NULL, any = NULL, target;
    DWORD seen = 0;
    HWND w = GetForegroundWindow();
    CERF_LOG_X("cerf_guest: tmpump switch target pid", pid);
    CERF_LOG_X("cerf_guest: tmpump switch fg hwnd", (DWORD)w);
    if (w) w = GetWindow(w, GW_HWNDFIRST);
    for (; w; w = GetWindow(w, GW_HWNDNEXT)) {
        DWORD wpid = 0;
        GetWindowThreadProcessId(w, &wpid);
        seen++;
        if (wpid != pid) continue;
        if (!any) any = w;
        if (IsWindowVisible(w)) {
            visible = w;
            break;
        }
    }
    CERF_LOG_X("cerf_guest: tmpump switch walk seen", seen);
    target = visible ? visible : any;
    if (!target) {
        CerfTmRespond(gen, 0, ERROR_NOT_FOUND, 0, 0);
        return;
    }
    SetForegroundWindow(target);
    CerfTmRespond(gen, 1, 0, 0, 0);
}

/* Enumerate top-level windows via the same GetWindow walk CerfTmDoSwitchTo
   uses (EnumWindows' callback marshaling rejects the kernel-loaded caller),
   one record per window. */
static void CerfTmDoListWindows(DWORD gen) {
    CerfTmWindowRecord rec;
    HWND  w = GetForegroundWindow();
    DWORD count = 0, total = 0;
    if (w) w = GetWindow(w, GW_HWNDFIRST);
    for (; w; w = GetWindow(w, GW_HWNDNEXT)) {
        DWORD pid = 0, tid, i;
        total++;
        if (count >= CERF_TM_MAX_RECORDS) continue;
        tid           = GetWindowThreadProcessId(w, &pid);
        rec.hwnd      = (DWORD)w;
        rec.pid       = pid;
        rec.thread_id = tid;
        rec.flags     = IsWindowVisible(w) ? CERF_TM_WINFLAG_VISIBLE : 0;
        for (i = 0; i < CERF_TM_WIN_TITLE_WCHARS; ++i) rec.title[i] = 0;
        GetWindowTextW(w, rec.title, CERF_TM_WIN_TITLE_WCHARS);
        CerfTmSendRecord((const ULONG*)&rec, sizeof(rec) / 4, count);
        count++;
    }
    CerfTmRespond(gen, 1, 0, count, total);
}

static void CerfTmDoSwitchToWin(DWORD gen, DWORD hwnd) {
    HWND w = (HWND)hwnd;
    if (!w || !IsWindow(w)) {
        CerfTmRespond(gen, 0, ERROR_NOT_FOUND, 0, 0);
        return;
    }
    SetForegroundWindow(w);
    CerfTmRespond(gen, 1, 0, 0, 0);
}

static BOOL CerfTmSpawn(const WCHAR* image, const WCHAR* args, DWORD* err) {
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(image, args, NULL, NULL, FALSE, 0, NULL, NULL, NULL,
                        &pi)) {
        *err = GetLastError();
        return FALSE;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return TRUE;
}

static void CerfTmDoRun(DWORD gen) {
    WCHAR cmd[CERF_TM_RUN_MAX + 1];
    WCHAR* image;
    WCHAR* args = NULL;
    WCHAR* p;
    BOOL ok;
    DWORD err = 0;
    DWORD len = s_tm_regs[CERF_TM_CMD_RUNLEN / 4];
    DWORD i;

    if (len == 0 || len > CERF_TM_RUN_MAX) {
        CerfTmRespond(gen, 0, ERROR_INVALID_PARAMETER, 0, 0);
        return;
    }
    for (i = 0; i < (len + 1) / 2; ++i) {
        ULONG v = s_tm_regs[(CERF_TM_CMD_RUNTEXT + i * 4) / 4];
        cmd[i * 2] = (WCHAR)(v & 0xFFFFu);
        if (i * 2 + 1 < len) cmd[i * 2 + 1] = (WCHAR)(v >> 16);
    }
    cmd[len] = 0;

    /* CE CreateProcess does not parse the image out of pszCmdLine. A quoted
       image carries its arguments after the closing quote; an unquoted line
       is tried whole first (CE paths contain spaces - "\Storage Card\...")
       and only split at the first space when that image does not exist. */
    image = cmd;
    if (cmd[0] == L'"') {
        image = cmd + 1;
        for (p = image; *p && *p != L'"'; ++p) {}
        if (*p) {
            *p++ = 0;
            while (*p == L' ') ++p;
            if (*p) args = p;
        }
        ok = CerfTmSpawn(image, args, &err);
    } else {
        ok = CerfTmSpawn(image, NULL, &err);
        if (!ok) {
            for (p = image; *p; ++p) {
                if (*p == L' ') {
                    *p   = 0;
                    args = p + 1;
                    break;
                }
            }
            if (args) ok = CerfTmSpawn(image, args, &err);
        }
    }
    CERF_LOG_X("cerf_guest: tmpump run result", ok ? 1u : err);
    CerfTmRespond(gen, ok ? 1u : 0u, err, 0, 0);
}

static DWORD WINAPI CerfTaskManagerPumpThread(LPVOID) {
    ULONG last_gen;

    s_tm_regs = (volatile ULONG*)CerfMapRegsPage(CerfVirt::kTaskManagerBase,
                                                 CerfVirt::kTaskManagerSize);
    if (!s_tm_regs) {
        CERF_LOG("cerf_guest: tmpump map FAILED");
        return 0;
    }

    last_gen = s_tm_regs[CERF_TM_CMD_GEN / 4];
    for (;;) {
        ULONG gen = s_tm_regs[CERF_TM_CMD_GEN / 4];
        if (gen != last_gen) {
            ULONG code = s_tm_regs[CERF_TM_CMD_CODE / 4];
            ULONG pid  = s_tm_regs[CERF_TM_CMD_PID / 4];
            last_gen = gen;
            switch (code) {
                case CERF_TM_OP_LIST:        CerfTmDoList(gen);            break;
                case CERF_TM_OP_KILL:        CerfTmDoKill(gen, pid);       break;
                case CERF_TM_OP_SWITCHTO:    CerfTmDoSwitchTo(gen, pid);   break;
                case CERF_TM_OP_RUN:         CerfTmDoRun(gen);             break;
                case CERF_TM_OP_LISTWINDOWS: CerfTmDoListWindows(gen);     break;
                case CERF_TM_OP_SWITCHTOWIN: CerfTmDoSwitchToWin(gen, pid); break;
                default:
                    CERF_LOG_X("cerf_guest: tmpump unknown cmd", code);
                    CerfTmRespond(gen, 0, ERROR_INVALID_PARAMETER, 0, 0);
                    break;
            }
        }
        Sleep(50);
    }
}

extern "C" void CerfStartTaskManagerPump(void) {
    static BOOL started = FALSE;
    HANDLE t;
    if (started) return;
    started = TRUE;
    t = CreateThread(NULL, 0, CerfTaskManagerPumpThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
