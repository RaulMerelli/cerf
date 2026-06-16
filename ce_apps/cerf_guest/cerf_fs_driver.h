#pragma once

#include "cerf_fs_protocol.h"

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CERF guest folder-share FSD: registers directly on the coredll AFS API-set
   (RegisterAFSName + CreateAPISet + RegisterAFS), no fsdmgr.dll, uniform CE3..CE7. */

/* The OAK <extfile.h> that declares this callback type fails to parse in this app
   build (SAL/security prereqs absent), so it is declared locally; the stored
   callback is never invoked. */
typedef BOOL (*SHELLFILECHANGEFUNC_t)(void* pfci);

/* ---- Per-object contexts ------------------------------------------------- */

/* The volume context handed to RegisterAFS; it arrives as the leading arg of
   every AFS-set method. One host folder == one volume, so a single instance. */
typedef struct CerfVol {
    int iAFS;                  /* AFS slot from RegisterAFSName, -1 == unmounted */
} CerfVol;

/* Per-open-file context behind a file-API-set handle (CreateAPIHandle). The
   host has no seek op — position rides each Read/Write — so the FSD tracks it. */
typedef struct CerfFile {
    unsigned short fHandle;    /* host-side open-file slot */
    unsigned long  pos;        /* current file position */
} CerfFile;

/* Per-search context behind a find-API-set handle (CreateAPIHandle). */
typedef struct CerfFind {
    unsigned long tid;         /* host-side find transaction id */
} CerfFind;

/* ---- AFS registration / lifecycle (cerf_fs_afs.c) ------------------------ */

/* Creates the three API-sets and starts the live mount/unmount poll thread.
   Called once from the driver-in-driver stream driver's CDD_Init (device.exe
   context). Idempotent. */
void CerfFsAfsInit(void);

/* ---- Change notification (cerf_fs_notify.c, CE5+) ------------------------ */

/* Creates the notify API-set used by AFS[17]. CE5+ only (needs SetEventData);
   silently no-ops where the export is absent. Called from CerfFsAfsInit. */
void   CerfFsNotifyInit(void);

/* AFS[17] FindFirstChangeNotificationW — returns the waitable event handle. */
HANDLE CerfFsFindFirstChangeNotificationW(CerfVol* vol, HANDLE hProc, PCWSTR path,
                                          BOOL subtree, DWORD filter);

/* API-set handles, created once by CerfFsAfsInit; the volume CreateFileW /
   FindFirstFileW methods mint handles off the file / find sets. */
extern HANDLE g_hCerfFileAPI;
extern HANDLE g_hCerfFindAPI;

/* Mints an API-set handle around an open-file/search context and (when hProc is
   NULL) transfers it to the current process — per FSDMGR AllocFSDHandle.
   Returns INVALID_HANDLE_VALUE on failure. */
HANDLE CerfFsMakeHandle(HANDLE apiSet, void* ctx, HANDLE hProc);

/* ---- Shared transport + state (cerf_fs_transport.c / cerf_fs_afs.c) ------ */

/* Maps the folder-share MMIO channel into the calling process (cached). */
volatile CerfFsChannel* CerfFsMapChannel(void);

/* Runs one ServerPB op synchronously (publishes ServerPB VA + op code, reads
   the result). Returns the result code and mirrors it into pb->fResult. */
unsigned long CerfFsCall(CerfFsServerPB* pb, unsigned long code);

/* The one shared ServerPB + 64K transfer buffer, allocated by CerfFsAfsInit;
   the lock serialises ops that reuse them. Valid for the driver's lifetime. */
CerfFsServerPB* CerfFsPb(void);
unsigned char*  CerfFsIoBuf(void);     /* CERF_FS_MAX_IO transfer buffer (fDTAPtr) */
void            CerfFsLock(void);
void            CerfFsUnlock(void);

/* Maps a host result code (CERF_FS_E_*) to a Win32 error + returns FALSE; maps
   CERF_FS_OK to TRUE without touching last-error. Shared by every op. */
int CerfFsResultToBool(unsigned long result);

/* DOS date/time (host fFileTimeDate form) <-> Win32 FILETIME; zeroed on fail. */
void CerfFsDosToFiletime(unsigned long dos_datetime, FILETIME* out);

/* ---- AFS API-set methods (leading arg == CerfVol*) — cerf_fs_vol.c ------- */
/* Indices + signatures per FSDMGR TABLES.C apfnAFSAPIs / asigAFSAPIs. */

BOOL   CerfFsCloseVolume(CerfVol* vol);                                  /* 0  */
BOOL   CerfFsCreateDirectoryW(CerfVol* vol, PCWSTR path, LPSECURITY_ATTRIBUTES sa); /* 2 */
BOOL   CerfFsRemoveDirectoryW(CerfVol* vol, PCWSTR path);               /* 3  */
DWORD  CerfFsGetFileAttributesW(CerfVol* vol, PCWSTR name);             /* 4  */
BOOL   CerfFsSetFileAttributesW(CerfVol* vol, PCWSTR name, DWORD attrs);/* 5  */
HANDLE CerfFsCreateFileW(CerfVol* vol, HANDLE hProc, PCWSTR name, DWORD access,
                         DWORD share, PSECURITY_ATTRIBUTES sa, DWORD create,
                         DWORD flags, HANDLE templ);                    /* 6  */
BOOL   CerfFsDeleteFileW(CerfVol* vol, PCWSTR name);                    /* 7  */
BOOL   CerfFsMoveFileW(CerfVol* vol, PCWSTR oldn, PCWSTR newn);         /* 8  */
HANDLE CerfFsFindFirstFileW(CerfVol* vol, HANDLE hProc, PCWSTR spec,
                            PWIN32_FIND_DATAW fd);                      /* 9  */
BOOL   CerfFsDeleteAndRenameFileW(CerfVol* vol, PCWSTR oldn, PCWSTR newn); /* 12 */
BOOL   CerfFsCloseAllFiles(CerfVol* vol, HANDLE hProc);                 /* 13 */
BOOL   CerfFsGetDiskFreeSpaceW(CerfVol* vol, PCWSTR path,
                               PDWORD pSectorsPerCluster, PDWORD pBytesPerSector,
                               PDWORD pFreeClusters, PDWORD pClusters);  /* 14 */
void   CerfFsNotify(CerfVol* vol, DWORD dwFlags);                      /* 15 */
BOOL   CerfFsRegisterFileSystemFunction(CerfVol* vol, SHELLFILECHANGEFUNC_t pfn); /* 16 */

/* ---- File API-set methods (leading arg == CerfFile*) — cerf_fs_file.c ---- */
/* Indices + signatures per FSDMGR TABLES.C apfnFileAPIs / asigFileAPIs. */

BOOL  CerfFsCloseFile(CerfFile* f);                                     /* 0  */
BOOL  CerfFsReadFile(CerfFile* f, PVOID buf, DWORD count, PDWORD done, OVERLAPPED* ov); /* 2 */
BOOL  CerfFsWriteFile(CerfFile* f, const void* buf, DWORD count, PDWORD done, OVERLAPPED* ov); /* 3 */
DWORD CerfFsGetFileSize(CerfFile* f, PDWORD sizeHigh);                  /* 4  */
DWORD CerfFsSetFilePointer(CerfFile* f, LONG dist, PLONG distHigh, DWORD method); /* 5 */
BOOL  CerfFsGetFileInformationByHandle(CerfFile* f, PBY_HANDLE_FILE_INFORMATION fi); /* 6 */
BOOL  CerfFsFlushFileBuffers(CerfFile* f);                             /* 7  */
BOOL  CerfFsGetFileTime(CerfFile* f, FILETIME* create, FILETIME* access, FILETIME* write); /* 8 */
BOOL  CerfFsSetFileTime(CerfFile* f, const FILETIME* create, const FILETIME* access,
                        const FILETIME* write);                        /* 9  */
BOOL  CerfFsSetEndOfFile(CerfFile* f);                                 /* 10 */
BOOL  CerfFsFileIoControl(CerfFile* f, DWORD code, PVOID pIn, DWORD inLen,
                          PVOID pOut, DWORD outLen, PDWORD pActualOut,
                          OVERLAPPED* ov);                             /* 11 */
BOOL  CerfFsReadFileWithSeek(CerfFile* f, PVOID buf, DWORD count, PDWORD done,
                             OVERLAPPED* ov, DWORD low, DWORD high);    /* 12 */
BOOL  CerfFsWriteFileWithSeek(CerfFile* f, const void* buf, DWORD count, PDWORD done,
                              OVERLAPPED* ov, DWORD low, DWORD high);   /* 13 */

/* ---- Find API-set methods (leading arg == CerfFind*) — cerf_fs_find.c ---- */
/* Indices + signatures per FSDMGR TABLES.C apfnFindAPIs / asigFindAPIs. */

BOOL CerfFsFindClose(CerfFind* s);                                     /* 0  */
BOOL CerfFsFindNextFileW(CerfFind* s, PWIN32_FIND_DATAW fd);           /* 2  */

#ifdef __cplusplus
}
#endif
