#pragma once

#include <windows.h>
#include <winddi.h>

extern const BYTE kCerfMixToRop3[17];

typedef struct _CerfGpeMode {
    ULONG modeId;
    ULONG width;
    ULONG height;
    ULONG bpp;
    ULONG frequency;
} CerfGpeMode;

typedef struct _CerfDisplayDevice {
    CerfGpeMode    mode;
    HSURF          hsurf;
    PALETTEENTRY   palette[256];
    unsigned short palette_entries;
} CerfDisplayDevice;

CerfDisplayDevice* CerfGetDisplayDevice(void);

extern "C" {
extern const int g_CerfHasAlphaBlend;
extern const int g_CerfHasGradientFill;
extern const int g_CerfHasAntiAliasedText;

DHPDEV  APIENTRY DrvEnablePDEV(DEVMODEW*, LPWSTR, ULONG, HSURF*, ULONG, ULONG*, ULONG,
                               DEVINFO*, HDEV, LPWSTR, HANDLE);
VOID    APIENTRY DrvDisablePDEV(DHPDEV);
HSURF   APIENTRY DrvEnableSurface(DHPDEV);
VOID    APIENTRY DrvDisableSurface(DHPDEV);
VOID    APIENTRY DrvDisableDriver(void);
HBITMAP APIENTRY DrvCreateDeviceBitmap(DHPDEV, SIZEL, ULONG);
VOID    APIENTRY DrvDeleteDeviceBitmap(DHSURF);
BOOL    APIENTRY DrvSetPalette(DHPDEV, PALOBJ*, FLONG, ULONG, ULONG);
ULONG   APIENTRY DrvGetModes(HANDLE, ULONG, DEVMODEW*);
ULONG*  APIENTRY DrvGetMasks(DHPDEV);
ULONG   APIENTRY DrvRealizeColor(USHORT, ULONG, ULONG*, ULONG);
ULONG            CerfSwapRedBlue(ULONG value);
ULONG   APIENTRY DrvUnrealizeColor(USHORT, ULONG, ULONG*, ULONG);
BOOL    APIENTRY DrvContrastControl(DHPDEV, ULONG, ULONG*);
VOID    APIENTRY DrvPowerHandler(DHPDEV, BOOL);
ULONG   APIENTRY DrvEscape(DHPDEV, SURFOBJ*, ULONG, ULONG, PVOID, ULONG, PVOID);
ULONG   APIENTRY DrvSetPointerShape(SURFOBJ*, SURFOBJ*, SURFOBJ*, XLATEOBJ*,
                                    LONG, LONG, LONG, LONG, RECTL*, FLONG);
VOID    APIENTRY DrvMovePointer(SURFOBJ*, LONG, LONG, RECTL*);

BOOL APIENTRY DrvBitBlt(SURFOBJ*, SURFOBJ*, SURFOBJ*, CLIPOBJ*, XLATEOBJ*, RECTL*,
                        POINTL*, POINTL*, BRUSHOBJ*, POINTL*, ROP4);
BOOL APIENTRY DrvCopyBits(SURFOBJ*, SURFOBJ*, CLIPOBJ*, XLATEOBJ*, RECTL*, POINTL*);
BOOL APIENTRY DrvAnyBlt(SURFOBJ*, SURFOBJ*, SURFOBJ*, CLIPOBJ*, XLATEOBJ*, POINTL*,
                        RECTL*, RECTL*, POINTL*, BRUSHOBJ*, POINTL*, ROP4, ULONG, ULONG);
BOOL APIENTRY DrvTransparentBlt(SURFOBJ*, SURFOBJ*, CLIPOBJ*, XLATEOBJ*, RECTL*, RECTL*, ULONG);
BOOL APIENTRY DrvPaint(SURFOBJ*, CLIPOBJ*, BRUSHOBJ*, POINTL*, MIX);
BOOL APIENTRY DrvRealizeBrush(BRUSHOBJ*, SURFOBJ*, SURFOBJ*, SURFOBJ*, XLATEOBJ*, ULONG);
BOOL APIENTRY DrvStrokePath(SURFOBJ*, PATHOBJ*, CLIPOBJ*, XFORMOBJ*, BRUSHOBJ*,
                            POINTL*, LINEATTRS*, MIX);
BOOL APIENTRY DrvFillPath(SURFOBJ*, PATHOBJ*, CLIPOBJ*, BRUSHOBJ*, POINTL*, MIX, FLONG);

BOOL APIENTRY CerfDrvGradientFill(SURFOBJ*, CLIPOBJ*, XLATEOBJ*, TRIVERTEX*, ULONG,
                                  PVOID, ULONG, RECTL*, POINTL*, ULONG);
BOOL APIENTRY CerfDrvAlphaBlend(SURFOBJ*, SURFOBJ*, CLIPOBJ*, XLATEOBJ*, RECTL*,
                                RECTL*, BLENDOBJ*);

void CerfSetVidBackingByOsMajor(unsigned long os_major);
}
