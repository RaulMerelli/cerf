#pragma once

#include <windows.h>

/* VirtualCopy is a coredll export not declared in the HPC Pro app-SDK
   headers; declare it with C linkage to match the export. PAGE_PHYSICAL /
   PAGE_NOCACHE come from <windows.h>. */
extern "C" BOOL VirtualCopy(LPVOID lpvDest, LPVOID lpvSrc,
                            DWORD cbSize, DWORD fdwProtect);

#define WIN_BYTES   0x00100000u            /* 1 MB map window           */
#define PAGE_BYTES  0x00001000u            /* 4 KB SEH read granularity */
#define WM_APP_DONE (WM_APP + 1)

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
} DumpState;

DWORD WINAPI DumpThread(LPVOID param);          /* dump.cpp  */
void         PaintProgress(HWND hwnd, DumpState* st);  /* paint.cpp */
