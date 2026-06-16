#include "cerf_fs_driver.h"

#include <windows.h>
#include <pkfuncs.h>      /* CreateAPISet, RegisterAPISet, CreateAPIHandle, PFNVOID */

/* AFS constants + coredll imports, inlined because the OAK headers that declare
   them don't parse in this app build. HT_FILE/HT_FIND/AFS_VERSION are stable
   CE3..CE7 (ce3/ce5 psyscall.h, WINCE600/700 SDK kfuncs.h); only the apiset sig
   WIDTH varies by version, handled in CerfCreateApiSets. */
#define AFS_VERSION    0x00000004
#define HT_FILE        7
#define HT_FIND        8
#define HT_AFSVOLUME   16    /* CE6+ file-system volume handle type (WINCE600 kfuncs.h:63) */
#define OID_FIRST_AFS  0     /* pre-reserved primary slot; stable CE3..CE7 */

/* pkfuncs.h macro-maps SetHandleOwner to a symbol absent from coredll.lib; call
   the plain exported function instead. These are coredll C exports — extern "C"
   so the references bind to the unmangled names. */
#undef SetHandleOwner
extern "C" {
BOOL SetHandleOwner(HANDLE h, HANDLE hProc);
int  RegisterAFSName(LPCWSTR pName);
BOOL DeregisterAFS(int index);
BOOL DeregisterAFSName(int index);
}

/* The AFS bind export is RegisterAFS (4 args) on CE3 but RegisterAFSEx (5 args,
   trailing flags) on CE4.2+; a static import of either fails to LOAD on the
   other family, so it is resolved from the live coredll at runtime. */
typedef BOOL (*PFN_RegisterAFS)(int, HANDLE, DWORD, DWORD);
typedef BOOL (*PFN_RegisterAFSEx)(int, HANDLE, DWORD, DWORD, DWORD);

static BOOL CerfRegisterAFS(int iAFS, HANDLE hApi, DWORD ctx) {
    HMODULE core = LoadLibraryW(L"coredll.dll");
    PFN_RegisterAFSEx ex;
    PFN_RegisterAFS reg;
    if (!core) return FALSE;
    ex = (PFN_RegisterAFSEx)GetProcAddressW(core, L"RegisterAFSEx");
    if (ex) return ex(iAFS, hApi, ctx, AFS_VERSION, 0);   /* flags 0 = visible mount */
    reg = (PFN_RegisterAFS)GetProcAddressW(core, L"RegisterAFS");
    if (reg) return reg(iAFS, hApi, ctx, AFS_VERSION);
    return FALSE;
}

/* CERF guest folder-share filesystem, registered on the coredll AFS API-set
   primitive (no fsdmgr.dll). Brought up from the driver-in-driver stream
   driver's CDD_Init (device.exe context). Live mount/unmount tracks the host
   channel's generation; the mount name is host-configurable. */

/* Table holds the CE6/7 max (24); the REGISTERED count is version-correct
   (CerfCreateApiSets). Registering fewer methods than the kernel dispatches faults
   its count guard — the CE5 FindFirstChangeNotification[17] vanish. */
#define CERF_AFS_METHODS   24
#define CERF_FILE_METHODS  14
#define CERF_FIND_METHODS  3

HANDLE g_hCerfFileAPI = NULL;
HANDLE g_hCerfFindAPI = NULL;

static HANDLE          s_hAFSAPI = NULL;
static CerfVol         s_vol = { -1 };
static CerfFsServerPB* s_pb = NULL;
static unsigned char*  s_iobuf = NULL;
static CRITICAL_SECTION s_cs;
static BOOL            s_inited = FALSE;

CerfFsServerPB* CerfFsPb(void)    { return s_pb; }
unsigned char*  CerfFsIoBuf(void) { return s_iobuf; }
void CerfFsLock(void)   { EnterCriticalSection(&s_cs); }
void CerfFsUnlock(void) { LeaveCriticalSection(&s_cs); }

int CerfFsResultToBool(unsigned long result) {
    DWORD w;
    if (result == CERF_FS_OK) return 1;
    switch (result) {
        case CERF_FS_E_FILE_NOT_FOUND: w = ERROR_FILE_NOT_FOUND;      break;
        case CERF_FS_E_PATH_NOT_FOUND: w = ERROR_PATH_NOT_FOUND;      break;
        case CERF_FS_E_ACCESS_DENIED:  w = ERROR_ACCESS_DENIED;       break;
        case CERF_FS_E_INVALID_HANDLE: w = ERROR_INVALID_HANDLE;      break;
        case CERF_FS_E_TOO_MANY_FILES: w = ERROR_TOO_MANY_OPEN_FILES; break;
        case CERF_FS_E_NO_MORE_FILES:  w = ERROR_NO_MORE_FILES;       break;
        case CERF_FS_E_INVALID_FUNC:   w = ERROR_INVALID_FUNCTION;    break;
        default:                       w = ERROR_GEN_FAILURE;         break;
    }
    SetLastError(w);
    return 0;
}

/* ---- Internal volume methods (AFS slots FSDMGR supplied itself) ---------- */

static SHELLFILECHANGEFUNC_t s_notify = NULL;

BOOL CerfFsCloseVolume(CerfVol* vol)               { (void)vol; return TRUE; }
/* Per-process file handles are SetHandleOwner'd to the opener, so the kernel
   auto-CloseFile's them on process exit; nothing extra to do here. */
BOOL CerfFsCloseAllFiles(CerfVol* vol, HANDLE hProc) { (void)vol; (void)hProc; return TRUE; }
void CerfFsNotify(CerfVol* vol, DWORD dwFlags)     { (void)vol; (void)dwFlags; }
BOOL CerfFsRegisterFileSystemFunction(CerfVol* vol, SHELLFILECHANGEFUNC_t pfn) {
    (void)vol; s_notify = pfn; return TRUE;
}

/* Per ODO fatfs: index 10 (RegisterFileSystemNotification) sub_1F74DB8 = return 0;
   index 11 (CeOidGetInfo) sub_1F74DC8 returns FALSE + SetLastError(87) on a volume
   with no object store. The kernel CALLS both, so neither may be a NULL slot. */
static BOOL CerfFsAfsReserved10(CerfVol* vol) {
    (void)vol;
    return FALSE;
}
static BOOL CerfFsAfsOidGetInfo(CerfVol* vol, DWORD oid, void* pInfo) {
    (void)vol; (void)oid; (void)pInfo;
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

/* FsIoControl(21) / FileSecurity(22,23): a host-folder volume has no custom FSCTLs
   or security descriptors, so an FSD declines them. One handler — each ignores its
   differently-shaped args (it reads none) and returns FALSE + not-supported. */
static BOOL CerfFsAfsNotSupported(CerfVol* vol) {
    (void)vol;
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

/* ---- API-set method tables (index order == FSDMGR TABLES.C) -------------- */

static const PFNVOID g_afsMethods[CERF_AFS_METHODS] = {
    (PFNVOID)CerfFsCloseVolume,            /* 0  CloseVolume               */
    (PFNVOID)NULL,                         /* 1  (reserved)                */
    (PFNVOID)CerfFsCreateDirectoryW,       /* 2  CreateDirectoryW          */
    (PFNVOID)CerfFsRemoveDirectoryW,       /* 3  RemoveDirectoryW          */
    (PFNVOID)CerfFsGetFileAttributesW,     /* 4  GetFileAttributesW        */
    (PFNVOID)CerfFsSetFileAttributesW,     /* 5  SetFileAttributesW        */
    (PFNVOID)CerfFsCreateFileW,            /* 6  CreateFileW               */
    (PFNVOID)CerfFsDeleteFileW,            /* 7  DeleteFileW               */
    (PFNVOID)CerfFsMoveFileW,              /* 8  MoveFileW                 */
    (PFNVOID)CerfFsFindFirstFileW,         /* 9  FindFirstFileW            */
    (PFNVOID)CerfFsAfsReserved10,          /* 10 fatfs return-0 stub       */
    (PFNVOID)CerfFsAfsOidGetInfo,          /* 11 CeOidGetInfo (decline)    */
    (PFNVOID)CerfFsDeleteAndRenameFileW,   /* 12 DeleteAndRenameFileW      */
    (PFNVOID)CerfFsCloseAllFiles,          /* 13 CloseAllFiles             */
    (PFNVOID)CerfFsGetDiskFreeSpaceW,      /* 14 GetDiskFreeSpaceW         */
    (PFNVOID)CerfFsNotify,                 /* 15 Notify                    */
    (PFNVOID)CerfFsRegisterFileSystemFunction, /* 16 RegisterFileSystemFunction */
    (PFNVOID)CerfFsFindFirstChangeNotificationW, /* 17 FindFirstChangeNotification (CE5+) */
    (PFNVOID)NULL,                         /* 18 FindNextChangeNotification (via notify handle) */
    (PFNVOID)NULL,                         /* 19 FindCloseChangeNotification (via notify handle) */
    (PFNVOID)NULL,                         /* 20 CeGetFileNotificationInfo */
    (PFNVOID)CerfFsAfsNotSupported,        /* 21 FsIoControl (CE5+)        */
    (PFNVOID)CerfFsAfsNotSupported,        /* 22 SetFileSecurityW (CE6+)   */
    (PFNVOID)CerfFsAfsNotSupported,        /* 23 GetFileSecurityW (CE6+)   */
};

static const PFNVOID g_fileMethods[CERF_FILE_METHODS] = {
    (PFNVOID)CerfFsCloseFile,                  /* 0  CloseFile                  */
    (PFNVOID)NULL,                             /* 1  (reserved)                 */
    (PFNVOID)CerfFsReadFile,                   /* 2  ReadFile                   */
    (PFNVOID)CerfFsWriteFile,                  /* 3  WriteFile                  */
    (PFNVOID)CerfFsGetFileSize,                /* 4  GetFileSize                */
    (PFNVOID)CerfFsSetFilePointer,             /* 5  SetFilePointer             */
    (PFNVOID)CerfFsGetFileInformationByHandle, /* 6  GetFileInformationByHandle */
    (PFNVOID)CerfFsFlushFileBuffers,           /* 7  FlushFileBuffers           */
    (PFNVOID)CerfFsGetFileTime,                /* 8  GetFileTime                */
    (PFNVOID)CerfFsSetFileTime,                /* 9  SetFileTime                */
    (PFNVOID)CerfFsSetEndOfFile,               /* 10 SetEndOfFile               */
    (PFNVOID)CerfFsFileIoControl,              /* 11 DeviceIoControl            */
    (PFNVOID)CerfFsReadFileWithSeek,           /* 12 ReadFileWithSeek           */
    (PFNVOID)CerfFsWriteFileWithSeek,          /* 13 WriteFileWithSeek          */
};

static const PFNVOID g_findMethods[CERF_FIND_METHODS] = {
    (PFNVOID)CerfFsFindClose,      /* 0  FindClose      */
    (PFNVOID)NULL,                 /* 1  (reserved)     */
    (PFNVOID)CerfFsFindNextFileW,  /* 2  FindNextFileW  */
};

/* CE3/CE5 signatures: 2-bit/arg, ARG_PTR=1, no count nibble (DWORD elements).
   Verified byte-identical to ODO fatfs.dll off_1F71068/0E8/130 and matching
   WINCE300/WINCE500 FSDMGR TABLES.C asig* tables. */
static const DWORD g_afsSig32[CERF_AFS_METHODS] = {
    0x000, 0x000, 0x014, 0x004, 0x004, 0x004, 0x410, 0x004,
    0x014, 0x050, 0x000, 0x010, 0x014, 0x000, 0x554, 0x000, 0x000,
    0x010,  /* 17 FindFirstChangeNotification FNSIG5(DW,DW,PTR,DW,DW) */
    0x000, 0x000, 0x000,                /* 18-20 (via notify handle) */
    0x5110,                             /* 21 FsIoControl FNSIG8       */
    0x000, 0x000,                       /* 22-23 CE6/7 only (64-bit)   */
};
static const DWORD g_fileSig32[CERF_FILE_METHODS] = {
    0x000, 0x000, 0x144, 0x144, 0x004, 0x010, 0x004, 0x000,
    0x054, 0x054, 0x000, 0x5110, 0x144, 0x144,
};
static const DWORD g_findSig32[CERF_FIND_METHODS] = {
    0x000, 0x000, 0x004,
};

/* CE6/CE7 signatures (64-bit). These drive cross-process AFS marshalling into
   our gwes server (SetupUmodeArgs): per-arg type maps the caller's strings/
   buffers (I_WSTR/IO_PTR/O_PDW), arg count sizes the server stack frame — a wrong
   count returns to a wild address. Per WINCE600 volumeapi.cpp AFSAPISigs. */
static const ULONGLONG g_afsSig64[CERF_AFS_METHODS] = {
    FNSIG1(DW),                                                  /* 0  CloseVolume          */
    FNSIG1(DW),                                                  /* 1  PreCloseVolume       */
    FNSIG5(DW, I_WSTR, I_WSTR, I_PTR, DW),                       /* 2  CreateDirectoryW     */
    FNSIG2(DW, I_WSTR),                                          /* 3  RemoveDirectoryW     */
    FNSIG2(DW, I_WSTR),                                          /* 4  GetFileAttributesW   */
    FNSIG3(DW, I_WSTR, DW),                                      /* 5  SetFileAttributesW   */
    FNSIG11(DW, DW, I_WSTR, DW, DW, PTR, DW, DW, DW, I_PTR, DW), /* 6  CreateFileW          */
    FNSIG2(DW, I_WSTR),                                          /* 7  DeleteFileW          */
    FNSIG3(DW, I_WSTR, I_WSTR),                                  /* 8  MoveFileW            */
    FNSIG5(DW, DW, I_WSTR, IO_PTR, DW),                          /* 9  FindFirstFileW       */
    FNSIG0(),                                                    /* 10 (CeRegisterFileSystemNotification) */
    FNSIG0(),                                                    /* 11 (CeOidGetInfo)       */
    FNSIG3(DW, I_WSTR, I_WSTR),                                  /* 12 DeleteAndRenameFileW */
    FNSIG2(DW, DW),                                              /* 13 CloseAllFiles        */
    FNSIG6(DW, I_WSTR, O_PDW, O_PDW, O_PDW, O_PDW),              /* 14 GetDiskFreeSpaceW    */
    FNSIG2(DW, DW),                                              /* 15 Notify               */
    FNSIG2(DW, DW),                                              /* 16 RegisterFileSystemFunction */
    FNSIG5(DW, DW, I_WSTR, DW, DW),                              /* 17 FindFirstChangeNotification */
    FNSIG0(),                                                    /* 18 FindNextChangeNotification */
    FNSIG0(),                                                    /* 19 FindCloseChangeNotification */
    FNSIG0(),                                                    /* 20 CeGetChangeNotificationInfo */
    FNSIG9(DW, DW, DW, IO_PTR, DW, IO_PTR, DW, O_PDW, IO_PDW),   /* 21 CeFsIoControl        */
    FNSIG5(DW, I_WSTR, DW, I_PTR, DW),                           /* 22 SetFileSecurityW     */
    FNSIG6(DW, I_WSTR, DW, O_PTR, DW, O_PDW),                    /* 23 GetFileSecurityW     */
};
/* File-handle signatures, matched to WINCE600 fileapi.cpp FileAPISigs — same
   cross-process marshalling contract as the volume sigs above. */
static const ULONGLONG g_fileSig64[CERF_FILE_METHODS] = {
    FNSIG1(DW),                                           /* 0  CloseFile         */
    FNSIG0(),                                             /* 1  (reserved)        */
    FNSIG5(DW, O_PTR, DW, O_PDW, IO_PDW),                 /* 2  ReadFile          */
    FNSIG5(DW, I_PTR, DW, O_PDW, IO_PDW),                 /* 3  WriteFile         */
    FNSIG2(DW, O_PDW),                                    /* 4  GetFileSize       */
    FNSIG4(DW, DW, IO_PDW, DW),                           /* 5  SetFilePointer    */
    FNSIG3(DW, O_PTR, DW),                                /* 6  GetFileInformationByHandle */
    FNSIG1(DW),                                           /* 7  FlushFileBuffers  */
    FNSIG4(DW, O_PI64, O_PI64, O_PI64),                   /* 8  GetFileTime       */
    FNSIG4(DW, IO_PI64, IO_PI64, IO_PI64),                /* 9  SetFileTime       */
    FNSIG1(DW),                                           /* 10 SetEndOfFile      */
    FNSIG8(DW, DW, IO_PTR, DW, IO_PTR, DW, O_PDW, IO_PDW),/* 11 DeviceIoControl   */
    FNSIG7(DW, O_PTR, DW, O_PDW, IO_PDW, DW, DW),         /* 12 ReadFileWithSeek  */
    FNSIG7(DW, I_PTR, DW, O_PDW, IO_PDW, DW, DW),         /* 13 WriteFileWithSeek */
};
static const ULONGLONG g_findSig64[CERF_FIND_METHODS] = {
    FNSIG1(DW),                                     /* FindClose            */
    FNSIG0(),                                       /* (reserved)           */
    FNSIG2(DW, PTR),                                /* FindNextFileW        */
};

/* ---- One-time API-set creation ------------------------------------------ */

/* CreateAPISet's coredll ordinal diverges by CE version (559 CE3/CE5, 2539
   CE6/CE7 — dumpbin /exports): a static by-ordinal import is unresolvable on
   the other family and fails the whole module load, so resolve it by name. */
typedef HANDLE (*PFN_CreateAPISet)(char*, USHORT, const PFNVOID*, const ULONGLONG*);
typedef BOOL   (*PFN_RegisterDirectMethods)(HANDLE, const PFNVOID*);

static BOOL CerfCreateApiSets(void) {
    OSVERSIONINFO ovi;
    BOOL wide;
    USHORT afsCount;
    HMODULE core = LoadLibraryW(L"coredll.dll");
    PFN_CreateAPISet pCreateAPISet =
        core ? (PFN_CreateAPISet)GetProcAddressW(core, L"CreateAPISet") : NULL;
    if (!pCreateAPISet) {
        CERF_LOG("cerf_guest: foldershare CreateAPISet unresolved");
        return FALSE;
    }

    ovi.dwOSVersionInfoSize = sizeof(ovi);
    GetVersionEx(&ovi);
    wide = (ovi.dwMajorVersion >= 6);   /* CE6+ uses the 64-bit sig encoding */
    /* CE6/7 explorer hangs on folder-open when more than 17 AFS methods are
       registered, so they stay at 17; only CE5/WM5 needs 22 to expose
       FindFirstChangeNotification at index 17. */
    afsCount = (ovi.dwMajorVersion == 5) ? 22 : 17;

    if (wide) {
        s_hAFSAPI     = pCreateAPISet("CFSV", afsCount,          g_afsMethods,  g_afsSig64);
        g_hCerfFileAPI = pCreateAPISet("CFSF", CERF_FILE_METHODS, g_fileMethods, g_fileSig64);
        g_hCerfFindAPI = pCreateAPISet("CFSS", CERF_FIND_METHODS, g_findMethods, g_findSig64);
    } else {
        s_hAFSAPI     = pCreateAPISet("CFSV", afsCount,          g_afsMethods,  (const ULONGLONG*)g_afsSig32);
        g_hCerfFileAPI = pCreateAPISet("CFSF", CERF_FILE_METHODS, g_fileMethods, (const ULONGLONG*)g_fileSig32);
        g_hCerfFindAPI = pCreateAPISet("CFSS", CERF_FIND_METHODS, g_findMethods, (const ULONGLONG*)g_findSig32);
    }
    if (!s_hAFSAPI || !g_hCerfFileAPI || !g_hCerfFindAPI) {
        CERF_LOG("cerf_guest: foldershare CreateAPISet FAILED");
        return FALSE;
    }
    CERF_LOG_X("cerf_guest: CFSV(AFS) apiset handle", (DWORD)s_hAFSAPI);
    CERF_LOG_X("cerf_guest: CFSF(file) apiset handle", (DWORD)g_hCerfFileAPI);
    CERF_LOG_X("cerf_guest: CFSS(find) apiset handle", (DWORD)g_hCerfFindAPI);
    /* File/find handles dispatch by handle TYPE: the apisets must claim
       HT_FILE / HT_FIND (per WINCE300 FSDMGR INIT.C) or every in-volume handle
       op returns ERROR_INVALID_HANDLE. */
    CERF_LOG_X("cerf_guest: RegisterAPISet HT_FILE ok",
               RegisterAPISet(g_hCerfFileAPI, HT_FILE | REGISTER_APISET_TYPE));
    CERF_LOG_X("cerf_guest: RegisterAPISet HT_FIND ok",
               RegisterAPISet(g_hCerfFindAPI, HT_FIND | REGISTER_APISET_TYPE));

    /* CE6+ only: RegisterDirectMethods installs unmarshalled method pointers,
       but our apiset server runs in gwes, so a cross-process caller's unmapped
       pointers corrupt the CE3 loader on exe launch. Version-gated because ODO
       CE3 coredll exports the symbol (a presence check enables it wrongly). */
    if (ovi.dwMajorVersion >= 6) {
        PFN_RegisterDirectMethods pRDM =
            (PFN_RegisterDirectMethods)GetProcAddressW(core, L"RegisterDirectMethods");
        if (pRDM) {
            CERF_LOG_X("cerf_guest: RegisterAPISet HT_AFSVOLUME ok",
                       RegisterAPISet(s_hAFSAPI, HT_AFSVOLUME | REGISTER_APISET_TYPE));
            CERF_LOG_X("cerf_guest: RegisterDirectMethods AFS ok",  pRDM(s_hAFSAPI, g_afsMethods));
            CERF_LOG_X("cerf_guest: RegisterDirectMethods file ok", pRDM(g_hCerfFileAPI, g_fileMethods));
        }
    }
    return TRUE;
}

HANDLE CerfFsMakeHandle(HANDLE apiSet, void* ctx, HANDLE hProc) {
    HANDLE h = CreateAPIHandle(apiSet, ctx);
    if (!h) return INVALID_HANDLE_VALUE;
    if (hProc == NULL) {
        hProc = GetCurrentProcess();
        if (hProc) SetHandleOwner(h, hProc);
    }
    return h;
}

/* ---- Live mount / unmount ------------------------------------------------ */

static void CerfReadMountName(volatile CerfFsChannel* ch, WCHAR* out, int cap) {
    int i;
    for (i = 0; i < cap - 1; ++i) {
        WCHAR c = (WCHAR)ch->MountPoint[i];
        if (!c) break;
        out[i] = c;
    }
    out[i] = 0;
}

/* Binds the volume apiset to slot iAFS and records it as mounted. */
static BOOL CerfBindSlot(int iAFS) {
    if (!CerfRegisterAFS(iAFS, s_hAFSAPI, (DWORD)&s_vol)) return FALSE;
    s_vol.iAFS = iAFS;
    CERF_LOG_X("cerf_guest: foldershare mounted iAFS", iAFS);
    return TRUE;
}

/* Claims an AFS name following FSDMGR_RegisterVolume: a free name is registered
   directly; a name filesys already reserved (e.g. "Storage Card") is claimed via
   the pre-reserved primary slot OID_FIRST_AFS; otherwise a numbered derivation. */
static void CerfMount(volatile CerfFsChannel* ch) {
    WCHAR base[64], cand[66];
    const WCHAR* n;
    int blen, iAFS, suffix;
    if (s_vol.iAFS != -1) return;               /* already mounted */
    CerfReadMountName(ch, base, 64);
    n = base;
    if (*n == L'\\' || *n == L'/') ++n;         /* AFS name has no leading sep */
    if (!*n) return;

    iAFS = RegisterAFSName(n);
    if (iAFS != -1 && GetLastError() == 0) {    /* free name */
        if (CerfBindSlot(iAFS)) return;
        DeregisterAFSName(iAFS);
        return;
    }
    if (CerfBindSlot(OID_FIRST_AFS)) return;    /* name reserved: take primary slot */

    blen = lstrlenW(n);                          /* both taken: derive Name2..Name9 */
    if (blen > 64) blen = 64;
    memcpy(cand, n, blen * sizeof(WCHAR));
    for (suffix = 2; suffix <= 9; ++suffix) {
        cand[blen] = (WCHAR)(L'0' + suffix);
        cand[blen + 1] = 0;
        iAFS = RegisterAFSName(cand);
        if (iAFS != -1 && GetLastError() == 0) {
            if (CerfBindSlot(iAFS)) return;
            DeregisterAFSName(iAFS);
        }
    }
    CERF_LOG("cerf_guest: foldershare mount FAILED (no free AFS name)");
}

static void CerfUnmount(void) {
    if (s_vol.iAFS == -1) return;
    DeregisterAFS(s_vol.iAFS);
    if (s_vol.iAFS > OID_FIRST_AFS)             /* primary slot's name is filesys's */
        DeregisterAFSName(s_vol.iAFS);
    CERF_LOG_X("cerf_guest: foldershare unmounted iAFS", s_vol.iAFS);
    s_vol.iAFS = -1;
}

static DWORD WINAPI CerfFsMountThread(LPVOID unused) {
    volatile CerfFsChannel* ch = CerfFsMapChannel();
    DWORD last_gen = 0xFFFFFFFFu;
    (void)unused;
    if (!ch) {
        CERF_LOG("cerf_guest: foldershare mount-thread no channel");
        return 0;
    }
    CERF_LOG("cerf_guest: foldershare mount-thread running");
    for (;;) {
        DWORD gen = ch->Generation;
        if (gen != last_gen) {
            last_gen = gen;
            /* Any config change: re-evaluate. A mount-point change while enabled
               is unmount-old then mount-new (the name is re-read on mount). */
            CerfUnmount();
            if (ch->Enabled) CerfMount(ch);
        }
        Sleep(200);
    }
}

void CerfFsAfsInit(void) {
    HANDLE t;
    if (s_inited) return;
    s_inited = TRUE;

    InitializeCriticalSection(&s_cs);
    s_pb = (CerfFsServerPB*)VirtualAlloc(0, sizeof(CerfFsServerPB),
                                         MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    s_iobuf = (unsigned char*)VirtualAlloc(0, CERF_FS_MAX_IO,
                                           MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!s_pb || !s_iobuf) {
        CERF_LOG("cerf_guest: foldershare buffer alloc FAILED");
        return;
    }
    memset(s_pb, 0, sizeof(*s_pb));
    s_pb->fStructureSize = sizeof(*s_pb);

    if (!CerfCreateApiSets()) return;
    CerfFsNotifyInit();   /* notify apiset for AFS[17]; no-ops pre-CE5 */

    t = CreateThread(NULL, 0, CerfFsMountThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
    CERF_LOG("cerf_guest: foldershare AFS init complete");
}

static DWORD WINAPI CerfFsDirectThread(LPVOID unused) {
    (void)unused;
    CerfFsAfsInit();
    return 0;
}

void CerfStartFolderShareDirect(void) {
    static BOOL started = FALSE;
    HANDLE t;
    if (started) return;
    started = TRUE;
    t = CreateThread(NULL, 0, CerfFsDirectThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
