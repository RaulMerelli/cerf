/* CE5-generation DirectDraw HAL structs (Ce5_ prefix), mirrored from CE5 ddrawi.h:
   its DDHALINFO field order/size differs from the CE6 ddrawi.h cerf_guest compiles
   against, so filling the CE6 layout into a CE5 runtime's buffer crashes the client.
   Layout validated vs stock ddraw_ipu_sdc.dll sub_318FE90 (dwNumHeaps@a1[22]=0). */
#pragma once

#include <windows.h>

#pragma pack(push, 4)

/* ddraw.h _DDPIXELFORMAT — 0x20 bytes (8 DWORDs); identical CE5/CE6. */
typedef struct _Ce5_DDPIXELFORMAT {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    union { DWORD dwRGBBitCount; DWORD dwYUVBitCount; DWORD dwAlphaBitDepth; };
    union { DWORD dwRBitMask; DWORD dwYBitMask; };
    union { DWORD dwGBitMask; DWORD dwUBitMask; };
    union { DWORD dwBBitMask; DWORD dwVBitMask; };
    union { DWORD dwRGBAlphaBitMask; DWORD dwYUVAlphaBitMask; };
} Ce5_DDPIXELFORMAT;

/* ddraw.h _DDSCAPS — single DWORD on CE. */
typedef struct _Ce5_DDSCAPS { DWORD dwCaps; } Ce5_DDSCAPS;

/* ddrawi.h _VIDMEMINFO — 0x50 bytes. Offsets per the ce5-oak comments. */
typedef struct _Ce5_VIDMEMINFO {
    DWORD               fpPrimary;          /* 0x00 FLATPTR primary surface */
    DWORD               dwFlags;            /* 0x04 */
    DWORD               dwDisplayWidth;     /* 0x08 */
    DWORD               dwDisplayHeight;    /* 0x0c */
    LONG                lDisplayPitch;      /* 0x10 */
    Ce5_DDPIXELFORMAT   ddpfDisplay;        /* 0x14 (0x20) */
    DWORD               dwOffscreenAlign;   /* 0x34 */
    DWORD               dwOverlayAlign;     /* 0x38 */
    DWORD               dwTextureAlign;     /* 0x3c */
    DWORD               dwZBufferAlign;     /* 0x40 */
    DWORD               dwAlphaAlign;       /* 0x44 */
    DWORD               dwNumHeaps;         /* 0x48 — 0 = HAL owns all video mem */
    DWORD               pvmList;            /* 0x4c LPVIDMEM — NULL when dwNumHeaps=0 */
} Ce5_VIDMEMINFO;

#define CE5_DD_ROP_SPACE (256 / 32)   /* ddraw.h DD_ROP_SPACE */

/* ddrawi.h _DDCORECAPS (inline in DDHALINFO; full field set for exact size). */
typedef struct _Ce5_DDCORECAPS {
    DWORD       dwSize;
    DWORD       dwCaps;
    DWORD       dwCaps2;
    DWORD       dwCKeyCaps;
    DWORD       dwFXCaps;
    DWORD       dwFXAlphaCaps;
    DWORD       dwPalCaps;
    DWORD       dwSVCaps;
    DWORD       dwAlphaBltConstBitDepths;
    DWORD       dwAlphaBltPixelBitDepths;
    DWORD       dwAlphaBltSurfaceBitDepths;
    DWORD       dwAlphaOverlayConstBitDepths;
    DWORD       dwAlphaOverlayPixelBitDepths;
    DWORD       dwAlphaOverlaySurfaceBitDepths;
    DWORD       dwZBufferBitDepths;
    DWORD       dwVidMemTotal;
    DWORD       dwVidMemFree;
    DWORD       dwMaxVisibleOverlays;
    DWORD       dwCurrVisibleOverlays;
    DWORD       dwNumFourCCCodes;
    DWORD       dwAlignBoundarySrc;
    DWORD       dwAlignSizeSrc;
    DWORD       dwAlignBoundaryDest;
    DWORD       dwAlignSizeDest;
    DWORD       dwAlignStrideAlign;
    DWORD       dwRops[CE5_DD_ROP_SPACE];
    Ce5_DDSCAPS ddsCaps;
    DWORD       dwMinOverlayStretch;
    DWORD       dwMaxOverlayStretch;
    DWORD       dwMinLiveVideoStretch;
    DWORD       dwMaxLiveVideoStretch;
    DWORD       dwMinHwCodecStretch;
    DWORD       dwMaxHwCodecStretch;
    DWORD       dwReserved1;
    DWORD       dwReserved2;
    DWORD       dwReserved3;
    DWORD       dwSVBCaps;
    DWORD       dwSVBCKeyCaps;
    DWORD       dwSVBFXCaps;
    DWORD       dwSVBRops[CE5_DD_ROP_SPACE];
    DWORD       dwVSBCaps;
    DWORD       dwVSBCKeyCaps;
    DWORD       dwVSBFXCaps;
    DWORD       dwVSBRops[CE5_DD_ROP_SPACE];
    DWORD       dwSSBCaps;
    DWORD       dwSSBCKeyCaps;
    DWORD       dwSSBFXCaps;
    DWORD       dwSSBRops[CE5_DD_ROP_SPACE];
    DWORD       dwMaxVideoPorts;
    DWORD       dwCurrVideoPorts;
    DWORD       dwSVBCaps2;
} Ce5_DDCORECAPS;

/* ddrawi.h _DDHAL_DDCALLBACKS — note the CE5 order/field set. */
typedef struct _Ce5_DDHAL_DDCALLBACKS {
    DWORD dwSize;
    DWORD dwFlags;
    PVOID DestroyDriver;
    PVOID CreateSurface;
    PVOID SetColorKey;
    PVOID SetMode;
    PVOID WaitForVerticalBlank;
    PVOID CanCreateSurface;
    PVOID CreatePalette;
    PVOID GetScanLine;
    PVOID SetExclusiveMode;     /* DX2 */
    PVOID FlipToGDISurface;     /* DX2 */
} Ce5_DDHAL_DDCALLBACKS;

/* Must be 18 DWORDs (72B): ddcore validator sub_3764D78 rejects
   lpDDSurfaceCallbacks with dwSize < 0x48 (→ NODIRECTDRAWSUPPORT). reserved5/6
   pad to that size, stay NULL (flag bits clear), and are never called. */
typedef struct _Ce5_DDHAL_DDSURFACECALLBACKS {
    DWORD dwSize;
    DWORD dwFlags;
    PVOID DestroySurface;
    PVOID Flip;
    PVOID SetClipList;
    PVOID Lock;
    PVOID Unlock;
    PVOID Blt;
    PVOID SetColorKey;
    PVOID AddAttachedSurface;
    PVOID GetBltStatus;
    PVOID GetFlipStatus;
    PVOID UpdateOverlay;
    PVOID SetOverlayPosition;
    PVOID reserved4;
    PVOID SetPalette;
    PVOID reserved5;            /* slot14 (bit 0x4000) — runtime size pad, unused */
    PVOID reserved6;            /* slot15 (bit 0x8000) — runtime size pad, unused */
} Ce5_DDHAL_DDSURFACECALLBACKS;

/* ddrawi.h _DDHALMODEINFO (ce5-oak ddrawi.h:1533) — 36 bytes. The CE5 ddcore
   loader unconditionally dereferences lpModeInfo for the primary mode dims/bpp
   (ddcore sub_376511C @0x37656F4 LDR [lpModeInfo]), so dwNumModes>=1 + a valid
   table is mandatory or it faults on our NULL. */
typedef struct _Ce5_DDHALMODEINFO {
    DWORD dwWidth;
    DWORD dwHeight;
    LONG  lPitch;
    DWORD dwBPP;
    WORD  wFlags;
    WORD  wRefreshRate;
    DWORD dwRBitMask;
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwAlphaBitMask;
} Ce5_DDHALMODEINFO;

/* ddrawi.h _DDHALINFO — 460 bytes. */
typedef struct _Ce5_DDHALINFO {
    DWORD           dwSize;                 /* a1[0]   = 460 */
    PVOID           lpDDCallbacks;          /* a1[1] */
    PVOID           lpDDSurfaceCallbacks;   /* a1[2] */
    PVOID           lpDDPaletteCallbacks;   /* a1[3] */
    Ce5_VIDMEMINFO  vmiData;                /* a1[4]  (0x50) */
    Ce5_DDCORECAPS  ddCaps;                 /* inline */
    DWORD           dwMonitorFrequency;
    PVOID           GetDriverInfo;          /* a1[104] */
    DWORD           dwModeIndex;
    PVOID           lpdwFourCC;
    DWORD           dwNumModes;
    PVOID           lpModeInfo;
    DWORD           dwFlags;
    PVOID           lpPDevice;
    DWORD           hInstance;
    ULONG_PTR       lpD3DGlobalDriverData;
    ULONG_PTR       lpD3DHALCallbacks;
    PVOID           lpDDExeBufCallbacks;
} Ce5_DDHALINFO;

/* Per-call DATA structs the CE5 runtime passes our callbacks. Only the two that
   differ from CE6 (and that the draw path uses) need reshaping; the rest share
   the CE6 head. */
typedef struct _Ce5_DDHAL_CREATESURFACEDATA {
    PVOID   lpDD;
    PVOID   lpDDSurfaceDesc;
    PVOID*  lplpSList;      /* CE5: SList BEFORE count (CE6 swaps these) */
    DWORD   dwSCnt;
    HRESULT ddRVal;
    PVOID   CreateSurface;  /* private */
} Ce5_DDHAL_CREATESURFACEDATA;

typedef struct _Ce5_DDHAL_LOCKDATA {
    PVOID   lpDD;
    PVOID   lpDDSurface;
    DWORD   bHasRect;
    RECTL   rArea;
    PVOID   lpSurfData;     /* return: screen-memory pointer */
    HRESULT ddRVal;
    PVOID   Lock;           /* private */
    DWORD   dwFlags;        /* CE5: dwFlags at END (CE6 has it after rArea) */
} Ce5_DDHAL_LOCKDATA;

#pragma pack(pop)
