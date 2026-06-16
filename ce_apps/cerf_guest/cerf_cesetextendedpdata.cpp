#include <windows.h>

#undef CeSetExtendedPdata

extern "C" BOOL CeSetExtendedPdata(LPVOID pData) {
    (void)pData;
    return TRUE;
}
