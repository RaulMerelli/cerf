/* No cache flush is needed under CERF - the JIT SMC bitmap re-translates a
   written code page on next execute. coredll exports CacheRangeFlush only on
   CE6+, so cerf_guest defines its own for the GENBLT blit-codegen path. */
#include <windows.h>

#undef CacheRangeFlush

extern "C" void CacheRangeFlush(LPVOID pAddr, DWORD dwLength, DWORD dwFlags) {
    (void)pAddr;
    (void)dwLength;
    (void)dwFlags;
}
