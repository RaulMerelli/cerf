#include <windows.h>
#include "cerf_regs_map.h"

/* Register offsets below MUST match cerf/peripherals/cerf_virt/cerf_virt_resize_regs.h. */
#include "cerf/peripherals/cerf_virt/cerf_virt_addr_map.h"

#define CERF_RSZ_WANT_W       0x00u
#define CERF_RSZ_WANT_H       0x04u
#define CERF_RSZ_WANT_BPP     0x08u
#define CERF_RSZ_WANT_GEN     0x0Cu
#define CERF_RSZ_APPLIED_W    0x10u
#define CERF_RSZ_APPLIED_H    0x14u
#define CERF_RSZ_APPLIED_GEN  0x18u

/* cerf_guest builds against the CE3 SDK (_WIN32_WCE=300), whose winuser.h has
   no ChangeDisplaySettingsEx (the API arrived in CE4+). Resolve it at runtime
   from coredll instead, and define its constants here. On CE3 the export is
   absent, so the pump self-disables - matching the CE3 fixed-resolution reality. */
#ifndef CDS_RESET
#define CDS_RESET 0x40000000u
#endif
#ifndef DISP_CHANGE_SUCCESSFUL
#define DISP_CHANGE_SUCCESSFUL 0
#endif
/* CE3-SDK DEVMODEW is 188 bytes and has no dmDisplayOrientation, so the pump writes
   it at raw offset 188 in the 192-byte buffer. Constant values: ce4.2 wingdi.h. */
#ifndef DM_DISPLAYORIENTATION
#define DM_DISPLAYORIENTATION 0x00800000u
#endif
#define CERF_DMDO_OFFSET 188u
#define CERF_DMDO_90      1u
#define CERF_DMDO_270     4u
typedef LONG (WINAPI *PFN_ChangeDisplaySettingsExW)(
    LPCWSTR, DEVMODEW*, HWND, DWORD, LPVOID);

/* Defined in main.cpp. SetMode allocates the primary at these, and
   CerfEnablePDEVWrap reports them as GDIINFO ulHorzRes/VertRes - which the gwes
   CDS applier copies into SM_CXSCREEN/CYSCREEN. So setting them to the target
   before the (marker-matching) CDS is what makes the new resolution take. */
extern ULONG g_FbWidth, g_FbHeight, g_FbBpp, g_FbStride;

static volatile ULONG* s_rsz_regs = NULL;

static BOOL CerfMapRszRegs(void) {
    if (s_rsz_regs) return TRUE;
    s_rsz_regs = (volatile ULONG*)CerfMapRegsPage(g_CerfVirtBase + CerfVirt::kResizeOffset,
                                                  CerfVirt::kResizeSize);
    return s_rsz_regs != NULL;
}

static DWORD WINAPI CerfResizePumpThread(LPVOID) {
    HMODULE h = LoadLibraryW(L"coredll.dll");
    PFN_ChangeDisplaySettingsExW cds = h
        ? (PFN_ChangeDisplaySettingsExW)GetProcAddressW(h, L"ChangeDisplaySettingsExW")
        : NULL;
    if (!cds && h)
        cds = (PFN_ChangeDisplaySettingsExW)GetProcAddressW(h, L"ChangeDisplaySettingsEx");
    CERF_LOG_X("cerf_guest: rszpump CDS proc", (DWORD)cds);
    if (!cds) {
        CERF_LOG("cerf_guest: rszpump no ChangeDisplaySettingsEx (CE3) - disabled");
        return 0;
    }
    if (!CerfMapRszRegs()) {
        CERF_LOG("cerf_guest: rszpump map FAILED");
        return 0;
    }

    /* Resize rides gwes's rotation apply path: after a rotation it re-queries the
       driver GDIINFO and resizes the desktop to it. cur flips each resize so the
       requested orientation differs from the current one, which gwes requires to
       fire the apply. (CE3 self-disabled above: no ChangeDisplaySettingsEx.) */
    ULONG base_bpp = g_FbBpp;
    ULONG applied_w = g_FbWidth, applied_h = g_FbHeight;
    int   cur = 0;
    ULONG last_gen = s_rsz_regs[CERF_RSZ_WANT_GEN / 4];

    for (;;) {
        ULONG gen = s_rsz_regs[CERF_RSZ_WANT_GEN / 4];
        if (gen != last_gen) {
            last_gen = gen;
            DWORD tw = s_rsz_regs[CERF_RSZ_WANT_W / 4];
            DWORD th = s_rsz_regs[CERF_RSZ_WANT_H / 4];
            if (tw == 0 || th == 0) { Sleep(50); continue; }

            g_FbWidth  = tw;
            g_FbHeight = th;
            g_FbStride = tw * (base_bpp >> 3);
            cur ^= 1;

            BYTE dmbuf[192];
            memset(dmbuf, 0, sizeof(dmbuf));
            DEVMODEW* dm = (DEVMODEW*)dmbuf;
            dm->dmSize   = 192;  /* gwes returns -2 if dmSize != its DEVMODEW size (192) */
            dm->dmFields = DM_DISPLAYORIENTATION;
            *(DWORD*)(dmbuf + CERF_DMDO_OFFSET) = (cur == 1) ? CERF_DMDO_270 : CERF_DMDO_90;

            LONG r = cds(NULL, dm, NULL, CDS_RESET, NULL);
            CERF_LOG_X("cerf_guest: rszpump CDS result", (DWORD)r);
            if (r == DISP_CHANGE_SUCCESSFUL) {
                applied_w = tw;
                applied_h = th;
                s_rsz_regs[CERF_RSZ_APPLIED_W / 4] = tw;
                s_rsz_regs[CERF_RSZ_APPLIED_H / 4] = th;
                s_rsz_regs[CERF_RSZ_APPLIED_GEN / 4] =
                    s_rsz_regs[CERF_RSZ_APPLIED_GEN / 4] + 1;
            } else {
                g_FbWidth  = applied_w;
                g_FbHeight = applied_h;
                g_FbStride = applied_w * (base_bpp >> 3);
                cur ^= 1;
            }
        }
        Sleep(50);
    }
}

extern "C" void CerfStartResizePump(void) {
    static BOOL started = FALSE;
    if (started) return;
    started = TRUE;
    HANDLE t = CreateThread(NULL, 0, CerfResizePumpThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
