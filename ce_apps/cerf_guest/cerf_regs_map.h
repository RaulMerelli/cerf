#ifndef CERF_REGS_MAP_H
#define CERF_REGS_MAP_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Map a cerf_virt MMIO register page (physical address, uncached).
   Returns the mapped VA or NULL; the mapping lives for the process. */
void* CerfMapRegsPage(ULONG pa, ULONG size);

#ifdef __cplusplus
}
#endif

#endif
