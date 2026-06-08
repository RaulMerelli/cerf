#include <windows.h>
#include <winddi.h>
#include <gpe.h>
#include <ctblt.h>

/* ClearType blt stub set: FALSE from ClearTypeBltInit tells GPE "stub
   library, no ClearType rendering". */

BOOL ClearTypeBltInit(GPEMode*, ULONG*, LPPALETTEENTRY, UINT) {
    return FALSE;
}

SCODE ClearTypeBltSelect(GPEBltParms*) {
    return 0;
}

BOOL GetGammaValue(ULONG* pGamma) {
    *pGamma = DEFAULT_CT_GAMMA;
    return FALSE;
}

BOOL SetGammaValue(ULONG, BOOL) {
    return FALSE;
}
