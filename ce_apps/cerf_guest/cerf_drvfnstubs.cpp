#include <windows.h>
#include <winddi.h>

/* Stand-in for drvalphablendstub/drvgradfillstub libs - no ARMv4 builds of
   them exist. GPE sets SB_* caps and routes blends through any non-NULL
   value here, bypassing cerf_guest's DRVENABLEDATA overrides; the stub
   contract is NULL. C++ linkage: gpe_lib imports the mangled names. */
PFN_DrvGradientFill pfnDrvGradientFill = NULL;
PFN_DrvAlphaBlend   pfnDrvAlphaBlend   = NULL;
