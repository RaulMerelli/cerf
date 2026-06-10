#include <windows.h>
#include <winioctl.h>   /* CTL_CODE / FILE_DEVICE_ACPI / METHOD_BUFFERED for pm.h */
#include <pm.h>

#include "cerf_debug_log.h"

/* CE5+ shells gate rendering on GetDevicePower(<Display>) <= D2; the PM only
   tracks a display advertising PMCLASS_DISPLAY (pm.h:45), so an injected cerf_guest
   must advertise or the query fails and the shell goes black. Host window is always
   full-on -> D0-only; CE3 has no PM (AdvertiseInterface absent) so this no-ops. */

extern "C" const wchar_t* CerfInjectedModuleName(void);

/* pm.h:45 PMCLASS_DISPLAY as a GUID (same value gemstone's GetDevicePower uses). */
static const GUID kPmClassDisplay =
    { 0xEB91C7C9, 0x8BF6, 0x4a2d, { 0x9A, 0xB8, 0x69, 0x72, 0x4E, 0xED, 0x97, 0xD1 } };

typedef BOOL (*PFN_AdvertiseInterface)(const GUID*, LPCWSTR, BOOL);

extern "C" void CerfAdvertiseDisplayPower(void) {
    /* Build the device name exactly as the GetDevicePower client does: "\Windows\"
       + HKLM\System\GDI\Drivers:Display (the injected display driver's filename). */
    wchar_t leaf[MAX_PATH];
    leaf[0] = 0;
    HKEY hk;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\GDI\\Drivers", 0, 0, &hk) == ERROR_SUCCESS) {
        DWORD type = 0, sz = sizeof(leaf);
        RegQueryValueExW(hk, L"Display", NULL, &type, (LPBYTE)leaf, &sz);
        leaf[MAX_PATH - 1] = 0;
        RegCloseKey(hk);
    }
    if (!leaf[0]) {
        const wchar_t* inj = CerfInjectedModuleName();
        if (inj) { int i = 0; for (; inj[i] && i < MAX_PATH - 1; ++i) leaf[i] = inj[i]; leaf[i] = 0; }
    }

    wchar_t name[MAX_PATH];
    wsprintfW(name, L"\\Windows\\%s", leaf);

    HMODULE h = LoadLibraryW(L"coredll.dll");
    PFN_AdvertiseInterface adv =
        h ? (PFN_AdvertiseInterface)GetProcAddressW(h, L"AdvertiseInterface") : NULL;
    if (!adv) {
        CERF_LOG("cerf_guest: no Power Manager (CE3) - display power advertise skipped");
        return;
    }
    BOOL ok = adv(&kPmClassDisplay, name, TRUE);
    CERF_LOG_X("cerf_guest: display power advertise ok", (DWORD)ok);
}

/* TRUE if iEsc is a PM power IOCTL this driver answers (for QUERYESCSUPPORT). */
extern "C" BOOL CerfIsPowerIoctl(ULONG iEsc) {
    return iEsc == IOCTL_POWER_CAPABILITIES || iEsc == IOCTL_POWER_GET
        || iEsc == IOCTL_POWER_SET || iEsc == IOCTL_POWER_QUERY;
}

/* Handle a PM power IOCTL arriving via DrvEscape (ExtEscape). Returns TRUE if
   iEsc was handled (with *pRet set), FALSE to let DrvEscape continue. */
extern "C" BOOL CerfPowerEscape(ULONG iEsc, ULONG cjOut, void* pvOut, ULONG* pRet) {
    switch (iEsc) {
    case IOCTL_POWER_CAPABILITIES:
        if (pvOut && cjOut >= sizeof(POWER_CAPABILITIES)) {
            POWER_CAPABILITIES* pc = (POWER_CAPABILITIES*)pvOut;
            memset(pc, 0, sizeof(*pc));
            pc->DeviceDx = (UCHAR)DX_MASK(D0);   /* host window: full-on only */
            *pRet = 1;
        } else {
            *pRet = (ULONG)-1;
        }
        return TRUE;
    case IOCTL_POWER_GET:
        if (pvOut && cjOut >= sizeof(CEDEVICE_POWER_STATE)) {
            *(CEDEVICE_POWER_STATE*)pvOut = D0;
            *pRet = 1;
        } else {
            *pRet = (ULONG)-1;
        }
        return TRUE;
    case IOCTL_POWER_SET:
        /* Only D0 is supported, so echo back the adjusted state D0 (pm.h:113). */
        if (pvOut && cjOut >= sizeof(CEDEVICE_POWER_STATE)) {
            *(CEDEVICE_POWER_STATE*)pvOut = D0;
            *pRet = 1;
        } else {
            *pRet = (ULONG)-1;
        }
        return TRUE;
    case IOCTL_POWER_QUERY:
        *pRet = (pvOut && cjOut >= sizeof(CEDEVICE_POWER_STATE)) ? 1u : (ULONG)-1;
        return TRUE;
    }
    return FALSE;
}
