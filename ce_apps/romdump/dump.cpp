/* Physical-memory dump worker: sweep [base, base+length) in 1 MB windows,
   map each with VirtualCopy(...PAGE_PHYSICAL), read page-by-page under SEH,
   write linearly so file offset == physical offset. */

#include "romdump.h"

/* Copy one page from a mapped physical window. An unpopulated static bank
   data-aborts on touch; catch it, fill 0xFF, and report the fill so the
   linear file keeps offset == PA (no skipped bytes). Kept in its own
   function so the SEH frame stays minimal and never wraps the file I/O. */
static BOOL ReadPageGuarded(BYTE* dst, const BYTE* src) {
    __try {
        memcpy(dst, src, PAGE_BYTES);
        return TRUE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        memset(dst, 0xFF, PAGE_BYTES);
        return FALSE;
    }
}

/* Bounded wide-string copy - CE has no lstrcpynW; keep it dependency-free. */
static void CopyStrW(LPWSTR dst, LPCWSTR src, int cch) {
    int i = 0;
    if (cch <= 0) return;
    while (i < cch - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

DWORD WINAPI DumpThread(LPVOID param) {
    DumpState* st = (DumpState*)param;
    HANDLE hf;
    BYTE*  buf;
    DWORD  off;

    hf = CreateFileW(st->outpath, GENERIC_WRITE, 0, NULL,
                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) {
        CopyStrW(st->err, L"Cannot create dump file (card read-only or full?).", 160);
        st->ok = 0; st->finished = 1;
        PostMessageW(st->hwnd, WM_APP_DONE, 0, 0);
        return 0;
    }

    buf = (BYTE*)LocalAlloc(LPTR, WIN_BYTES);
    if (!buf) {
        CloseHandle(hf);
        CopyStrW(st->err, L"Out of memory allocating window buffer.", 160);
        st->ok = 0; st->finished = 1;
        PostMessageW(st->hwnd, WM_APP_DONE, 0, 0);
        return 0;
    }

    for (off = 0; off < st->length; off += WIN_BYTES) {
        DWORD pa = st->base + off;
        void* va;
        BOOL  mapped = FALSE;
        DWORD p, wr = 0;

        if (st->cancel) break;   /* Exit pressed - stop, close file cleanly */

        st->cur_pa = pa;

        /* PAGE_PHYSICAL maps by page-frame number, so the source argument is
           the physical address shifted right by 8 (matches CERF's own guest
           driver, ce_apps/cerf_guest). PAGE_NOCACHE avoids reading a stale
           cached copy of flash/MMIO. */
        va = VirtualAlloc(NULL, WIN_BYTES, MEM_RESERVE, PAGE_NOACCESS);
        if (va) {
            mapped = VirtualCopy(va, (LPVOID)(pa >> 8), WIN_BYTES,
                                 PAGE_READWRITE | PAGE_NOCACHE | PAGE_PHYSICAL);
        }

        for (p = 0; p < WIN_BYTES; p += PAGE_BYTES) {
            if (mapped) {
                if (!ReadPageGuarded(buf + p, (BYTE*)va + p)) st->fault_pages++;
            } else {
                memset(buf + p, 0xFF, PAGE_BYTES);
                st->fault_pages++;
            }
        }

        if (va) VirtualFree(va, 0, MEM_RELEASE);

        if (!WriteFile(hf, buf, WIN_BYTES, &wr, NULL) || wr != WIN_BYTES) {
            wsprintfW(st->err,
                      L"Write failed at PA 0x%08X (card full after %u MB?).",
                      pa, (unsigned)(off >> 20));
            LocalFree(buf);
            CloseHandle(hf);
            st->ok = 0; st->finished = 1;
            PostMessageW(st->hwnd, WM_APP_DONE, 0, 0);
            return 0;
        }

        st->bytes_done = off + WIN_BYTES;
    }

    LocalFree(buf);
    FlushFileBuffers(hf);
    CloseHandle(hf);
    st->ok = !st->cancel;   /* fully completed only if not cancelled */
    st->finished = 1;
    PostMessageW(st->hwnd, WM_APP_DONE, 0, 0);
    return 0;
}
