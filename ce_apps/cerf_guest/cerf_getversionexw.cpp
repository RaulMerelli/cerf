/* CE 2.0 coredll exports GetVersionEx but not the GetVersionExW alias a CE6 GPE
   lib references; this local definition wins over the coredll import and
   forwards to GetVersionEx (present on every CE). */
#include <windows.h>

#undef GetVersionEx

extern "C" BOOL WINAPI GetVersionEx(LPOSVERSIONINFOW);

extern "C" BOOL WINAPI GetVersionExW(LPOSVERSIONINFOW lpvi) {
    return GetVersionEx(lpvi);
}
