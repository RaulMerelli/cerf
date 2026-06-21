/* Windows Mobile (CE 5.1/5.2 kernel, CE6 rendering engine) DirectDraw HAL:
   CE6-engine callback offsets but a 252-byte DDHALINFO / 112-byte DDCAPS - 16B
   smaller than CE6/CE7 (284/128). Sizing it to 284 makes the WM ddcore reject
   the HAL. Verified vs stock DeviceEmulator_lcd.dll (WM5/WM6/WM6.5: *a1=252). */
#pragma once

#include <windows.h>

#pragma pack(push, 4)

#define WM_DD_ROP_SPACE (256 / 32)   /* ddraw.h DD_ROP_SPACE */

typedef struct _Wm_DDSCAPS { DWORD dwCaps; } Wm_DDSCAPS;

/* ce6-oak DDCAPS minus its 4 trailing video-port fields = 28 DWORDs / 112B
   (stock ddCaps.dwSize). Restoring them makes it 128 and the WM ddcore rejects. */
typedef struct _Wm_DDCAPS {
    DWORD       dwSize;
    DWORD       dwVidMemTotal;
    DWORD       dwVidMemFree;
    DWORD       dwVidMemStride;
    Wm_DDSCAPS  ddsCaps;
    DWORD       dwNumFourCCCodes;
    DWORD       dwPalCaps;
    DWORD       dwBltCaps;
    DWORD       dwCKeyCaps;
    DWORD       dwAlphaCaps;
    DWORD       dwRops[WM_DD_ROP_SPACE];
    DWORD       dwOverlayCaps;
    DWORD       dwMaxVisibleOverlays;
    DWORD       dwCurrVisibleOverlays;
    DWORD       dwAlignBoundarySrc;
    DWORD       dwAlignSizeSrc;
    DWORD       dwAlignBoundaryDest;
    DWORD       dwAlignSizeDest;
    DWORD       dwMinOverlayStretch;
    DWORD       dwMaxOverlayStretch;
    DWORD       dwMiscCaps;
} Wm_DDCAPS;

typedef struct _Wm_DDHALINFO {      /* 252B / 63 DWORDs; callbacks at CE6 offsets */
    DWORD       dwSize;                  /* a1[0] = 252 */
    DWORD       dwFlags;                 /* a1[1] */
    PVOID       lpDDCallbacks;           /* a1[2] */
    PVOID       lpDDSurfaceCallbacks;    /* a1[3] */
    PVOID       lpDDPaletteCallbacks;    /* a1[4] */
    PVOID       GetDriverInfo;           /* a1[5] */
    Wm_DDCAPS   ddCaps;                  /* a1[6..33]  (112) */
    Wm_DDCAPS   ddHelCaps;               /* a1[34..61] (112) */
    PVOID       lpdwFourCC;              /* a1[62] */
} Wm_DDHALINFO;

#pragma pack(pop)
