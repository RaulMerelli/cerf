#pragma once

#include <windows.h>

/* VirtualCopy is a coredll export not declared in the HPC Pro app-SDK
   headers; declare it with C linkage to match the export. PAGE_PHYSICAL /
   PAGE_NOCACHE come from <windows.h>. */
extern "C" BOOL VirtualCopy(LPVOID lpvDest, LPVOID lpvSrc,
                            DWORD cbSize, DWORD fdwProtect);

#define WIN_BYTES    0x00100000u           /* 1 MB map window           */
#define PAGE_BYTES   0x00001000u           /* 4 KB SEH read granularity */
#define WM_APP_DONE    (WM_APP + 1)
#define WM_APP_SEGMENT (WM_APP + 2)        /* worker asks UI to prompt for next seg */

typedef struct {
    HWND          hwnd;
    HANDLE        thread;
    WCHAR         outpath[MAX_PATH];
    DWORD         base;
    DWORD         length;
    volatile DWORD cur_pa;
    volatile DWORD bytes_done;
    volatile DWORD fault_pages;            /* pages filled 0xFF (no device) */
    volatile int   cancel;
    volatile int   running;
    volatile int   finished;
    volatile int   ok;
    WCHAR         err[160];

    int            segmented;              /* 1 = per-segment files + pause between */
    DWORD          seg_bytes;              /* segment size in bytes (whole MB)       */
    volatile DWORD seg_index;              /* segment just written, 1-based (prompt) */
    volatile DWORD segs_written;           /* fully-written segment files so far     */
    volatile int   seg_continue;           /* UI thread's Yes(1)/No(0) to the prompt */
    HANDLE         seg_event;              /* auto-reset: UI wakes the worker        */
} DumpState;

DWORD WINAPI DumpThread(LPVOID param);          /* dump.cpp  */
void         PaintProgress(HWND hwnd, DumpState* st);  /* paint.cpp */
