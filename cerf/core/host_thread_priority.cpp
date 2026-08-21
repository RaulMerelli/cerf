#include "host_thread_priority.h"

#include "cerf_emulator.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

REGISTER_SERVICE(HostThreadPriority);

namespace {

constexpr DWORD kFirstElevatingMajor = 6;

DWORD HostMajorVersion() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) return 0;
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    const auto fn = reinterpret_cast<RtlGetVersionFn>(
        GetProcAddress(ntdll, "RtlGetVersion"));
    if (fn == nullptr) return 0;
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (fn(&info) != 0) return 0;
    return info.dwMajorVersion;
}

}

void HostThreadPriority::OnReady() {
    const DWORD major = HostMajorVersion();
    elevate_ = major >= kFirstElevatingMajor;
    LOG(Boot, "HostThreadPriority: host major version %lu, emulation-thread "
        "elevation %s\n", major, elevate_ ? "on" : "off");
}

void HostThreadPriority::Elevate(HostThreadRole role) const {
    if (!elevate_) return;
    SetThreadPriority(GetCurrentThread(),
                      role == HostThreadRole::TimerExpiry
                          ? THREAD_PRIORITY_TIME_CRITICAL
                          : THREAD_PRIORITY_HIGHEST);
}
