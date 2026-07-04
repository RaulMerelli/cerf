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

/* Map one 1 MB physical window at pa and read it into buf. PAGE_PHYSICAL maps
   by page-frame number, so the source is the physical address >> 8; PAGE_NOCACHE
   avoids a stale cached copy of flash/MMIO. Returns the number of pages that
   data-aborted (filled 0xFF) so the caller's file offset stays == PA. */
static DWORD MapAndReadWindow(DWORD pa, BYTE* buf) {
    void* va;
    BOOL  mapped = FALSE;
    DWORD p, faults = 0;

    va = VirtualAlloc(NULL, WIN_BYTES, MEM_RESERVE, PAGE_NOACCESS);
    if (va)
        mapped = VirtualCopy(va, (LPVOID)(pa >> 8), WIN_BYTES,
                             PAGE_READWRITE | PAGE_NOCACHE | PAGE_PHYSICAL);
    for (p = 0; p < WIN_BYTES; p += PAGE_BYTES) {
        if (mapped) {
            if (!ReadPageGuarded(buf + p, (BYTE*)va + p)) faults++;
        } else {
            memset(buf + p, 0xFF, PAGE_BYTES);
            faults++;
        }
    }
    if (va) VirtualFree(va, 0, MEM_RELEASE);
    return faults;
}

DWORD WINAPI DumpThread(LPVOID param) {
    DumpState* st = (DumpState*)param;
    BYTE*  buf;
    DWORD  seg_bytes, num_segs, s;
    int    failed = 0, stopped = 0;

    buf = (BYTE*)LocalAlloc(LPTR, WIN_BYTES);
    if (!buf) {
        CopyStrW(st->err, L"Out of memory allocating window buffer.", 160);
        st->ok = 0; st->finished = 1;
        PostMessageW(st->hwnd, WM_APP_DONE, 0, 0);
        return 0;
    }

    /* seg_bytes and length are whole megabytes, so this_len is always a whole
       number of 1 MB windows - the WriteFile below writes a full WIN_BYTES per
       window without overrunning the segment. Non-segmented dumps one segment. */
    seg_bytes = st->segmented ? st->seg_bytes : st->length;
    if (seg_bytes == 0) seg_bytes = st->length;
    num_segs = (st->length + seg_bytes - 1) / seg_bytes;
    if (num_segs == 0) num_segs = 1;

    for (s = 0; s < num_segs; s++) {
        WCHAR  path[MAX_PATH + 8];
        HANDLE hf;
        DWORD  seg_off  = s * seg_bytes;
        DWORD  this_len = st->length - seg_off;
        DWORD  w;

        if (st->cancel) break;
        if (this_len > seg_bytes) this_len = seg_bytes;

        if (st->segmented)
            wsprintfW(path, L"%s.%03u", st->outpath, (unsigned)(s + 1));
        else
            CopyStrW(path, st->outpath, MAX_PATH + 8);

        hf = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hf == INVALID_HANDLE_VALUE) {
            wsprintfW(st->err, L"Cannot create %s (read-only or full?).", path);
            failed = 1; break;
        }

        for (w = 0; w < this_len; w += WIN_BYTES) {
            DWORD pa = st->base + seg_off + w;
            DWORD wr = 0;
            if (st->cancel) break;
            st->cur_pa = pa;
            st->fault_pages += MapAndReadWindow(pa, buf);
            if (!WriteFile(hf, buf, WIN_BYTES, &wr, NULL) || wr != WIN_BYTES) {
                wsprintfW(st->err, L"Write failed at PA 0x%08X (storage full?).", pa);
                failed = 1; break;
            }
            st->bytes_done = seg_off + w + WIN_BYTES;
        }

        FlushFileBuffers(hf);
        CloseHandle(hf);
        if (failed || st->cancel) break;
        st->segs_written++;

        /* Pause after every segment except the last: hand off to the UI thread
           to prompt, then block until it answers (Exit also signals the event). */
        if (st->segmented && s + 1 < num_segs) {
            st->seg_index = s + 1;
            st->seg_continue = 0;
            PostMessageW(st->hwnd, WM_APP_SEGMENT, 0, 0);
            WaitForSingleObject(st->seg_event, INFINITE);
            if (!st->seg_continue) { stopped = 1; break; }
        }
    }

    LocalFree(buf);
    st->ok = (!failed && !st->cancel && !stopped);
    st->finished = 1;
    PostMessageW(st->hwnd, WM_APP_DONE, 0, 0);
    return 0;
}
