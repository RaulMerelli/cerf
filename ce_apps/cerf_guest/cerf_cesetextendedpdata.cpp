/* Pocket PC 2000 coredll exports CeSetExtendedPdata by ordinal only, and
   cerf_guest imports coredll by name, so this symbol must be local or the body
   won't load on PPC2000. GENBLT calls it to register unwind pdata for its
   generated blit code (genblt.cpp:194), consulted only on a fault there. */
#include <windows.h>

#undef CeSetExtendedPdata

extern "C" BOOL CeSetExtendedPdata(LPVOID pData) {
    (void)pData;
    return TRUE;
}
