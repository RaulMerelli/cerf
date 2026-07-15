#pragma once

#include <windows.h>
#include <pkfuncs.h>
#include <winddi.h>
#include "include/cerf_gpe.h"
#include "cerf/peripherals/cerf_virt/cerf_virt_blt_descriptor.h"
#include "cerf/peripherals/cerf_virt/cerf_virt_line_descriptor.h"

extern void* CerfMapFbMemory(void);
extern "C" void* CerfMapFbGlobal(void);
extern "C" ULONG CerfGpeFbMemBasePa(void);
extern "C" ULONG CerfGpeBlt(ULONG desc_va);
extern "C" ULONG CerfGpeLine(ULONG desc_va);
extern "C" void* CerfMapFbWindow(ULONG fb_pa, ULONG bytes);
extern "C" void  CerfUnmapFbWindow(void* va);
extern "C" void  CerfPublishPalette(const ULONG* rgb, unsigned first, unsigned count);
extern "C" void CerfPublishCursor(const void* mask_bits, int stride,
                                  int cx, int cy, int xhot, int yhot, BOOL visible);
extern ULONG g_FbWidth, g_FbHeight, g_FbBpp, g_FbStride, g_FbMemTotal, g_FbPrimaryReserve;
extern ULONG g_OsMajor;
extern ULONG g_FbDpi;

struct CerfStageWb { BOOL active; ULONG dst_va; void* arena_ptr; ULONG span; };

ULONG CerfSpanBytes(int x0, int y0, int x1, int y1, int stride, int bits);

inline bool CerfConvertibleFmt(EGPEFormat f) {
    return f == gpe1Bpp  || f == gpe2Bpp  || f == gpe4Bpp || f == gpe8Bpp ||
           f == gpe16Bpp || f == gpe24Bpp || f == gpe32Bpp;
}

inline int CerfFormatBpp(EGPEFormat fmt) {
    switch (fmt) {
        case gpe1Bpp:  return 1;
        case gpe2Bpp:  return 2;
        case gpe4Bpp:  return 4;
        case gpe8Bpp:  return 8;
        case gpe16Bpp: return 16;
        case gpe24Bpp: return 24;
        case gpe32Bpp: return 32;
        default:       return 32;
    }
}

enum CerfVidBacking { kCerfVidGuestRamHeap, kCerfVidGlobalFb };

class CerfDDGPE : public DDGPE {
public:
    CerfDDGPE();

    void          SetVidBacking(CerfVidBacking b) { m_vidBacking = b; }
    CerfVidBacking VidBacking() const             { return m_vidBacking; }

    bool EnsureVideoHeap();
    void GetVirtualVideoMemory(unsigned long* base, unsigned long* size,
                               unsigned long* freeBytes);
    bool SurfaceFbPa(GPESurf* s, ULONG* pa);
    SCODE ApplyFbMode();

    DDGPESurf* EnsurePrimaryShadow();
    void       PrimaryShadowPresent();

    virtual SCODE BltPrepare(GPEBltParms* p);
    static void RectToDesc(CerfVirt::CerfBltRect* r, const RECTL* s);
    void FillSurface(CerfVirt::CerfBltSurface* s, GPESurf* surf,
                     int x0, int y0, int x1, int y1, bool host_writes,
                     bool read_palette = true, CerfStageWb* wb = NULL);
    void FillSurfaceFromSurfobj(CerfVirt::CerfBltSurface* s, SURFOBJ* pso,
                                int y0, int y1, CerfStageWb* wb = NULL);
    SCODE HwBlt(GPEBltParms* p);
    SCODE HostLine(GPELineParms* p);

    virtual SCODE BltComplete(GPEBltParms* p);
    virtual SCODE Line(GPELineParms* pLineParms, EGPEPhase phase);
    virtual SCODE AllocSurface(GPESurf** ppSurf, int width, int height,
                               EGPEFormat format, int surfaceFlags);
    virtual void SetVisibleSurface(GPESurf* pSurf, BOOL bWaitForVBlank = FALSE);
    virtual SCODE SetPointerShape(GPESurf* pMask, GPESurf*, int xHot, int yHot,
                                  int cx, int cy);
    virtual SCODE MovePointer(int, int);
    virtual SCODE SetPalette(const PALETTEENTRY* src, unsigned short firstEntry,
                             unsigned short numEntries);
    virtual SCODE GetPalette(PALETTEENTRY** ppPalette, unsigned short* pcEntries);
    virtual SCODE GetModeInfo(GPEMode* pMode, int modeNo);
    virtual int NumModes();
    virtual SCODE SetMode(int modeId, HPALETTE* pPalette);
    virtual int InVBlank();
    virtual ULONG GetGraphicsCaps();
    virtual ULONG DrvEscape(SURFOBJ* pso, ULONG iEsc, ULONG cjIn, PVOID pvIn,
                            ULONG cjOut, PVOID pvOut);
    virtual BOOL IsPaletteSettable();
    virtual BOOL GetScreenDimensions(GPEScreenProps* pProps);
    virtual ULONG* GetClearTypeRGBMasks();

private:
    GPEMode         m_gpeMode;
    unsigned short  m_paletteEntries;
    PALETTEENTRY    m_palette[256];
    SurfaceHeap*    m_pVidHeap;
    BYTE*           m_vidBaseVa;
    ULONG           m_vidSize;
    CerfVidBacking  m_vidBacking;
    DDGPESurf*      m_pPrimaryShadow;
    int             m_currentRotation;
};
