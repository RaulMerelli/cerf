#pragma once

#include <windows.h>

/* Guest debug-log transport: a per-process MMIO channel the host drains into
   cerf.log. Two tiers - CERF_LOG is always-on so the driver's init / activation /
   mount / error trail reaches cerf.log in production builds too; CERF_LOG_DEV is
   the dev-only tier for per-operation graphics tracing. */

#ifdef __cplusplus
extern "C" {
#endif
void CerfInitLogging(unsigned long id);
void CerfDebugTx(const char* msg);
void CerfDebugTxX(const char* msg, DWORD value);
#ifdef __cplusplus
}
#endif

/* Log-channel ids passed to CERF_LOG_INIT; mirror host CerfVirt::kLogChannelId*. */
enum {
    CERF_LOG_CH_STUB           = 0,   /* cerf_guest_stub injection carrier */
    CERF_LOG_CH_DISPLAY        = 1,   /* gwes display driver */
    CERF_LOG_CH_SHARED_FOLDERS = 2,   /* device.exe shared-folders FSD carrier */
};

/* Normal driver logs (init, activation, driver-in-driver entrypoint, FSD mount,
   errors): always emitted, so the guest trail reaches cerf.log in production
   builds too when a user reports a guest-additions failure. */
#define CERF_LOG(msg)        CerfDebugTx(msg)
#define CERF_LOG_X(msg, val) CerfDebugTxX((msg), (DWORD)(val))
#define CERF_LOG_INIT(id)    CerfInitLogging((unsigned long)(id))

/* Per-operation graphics tracing (per-blit / per-escape / per-cursor / per-mode):
   high-frequency, dev-build only - stripped at the call site in production. */
#if CERF_DEV_MODE
#define CERF_LOG_DEV(msg)        CerfDebugTx(msg)
#define CERF_LOG_X_DEV(msg, val) CerfDebugTxX((msg), (DWORD)(val))
#else
#define CERF_LOG_DEV(msg)        ((void)0)
#define CERF_LOG_X_DEV(msg, val) ((void)0)
#endif
