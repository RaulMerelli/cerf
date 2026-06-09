#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class HostWindow : public Service {
public:
    using Service::Service;
    ~HostWindow() override;
    void OnReady() override;
    void OnShutdown() override;

    /* SoC LCD service calls on guest panel-enable edge. fb_(w|h) are raw
       LCD-controller dimensions; run through the FrameRenderer's HostSizeFor
       so rotating renderers can swap. */
    void OnLcdEnabled(uint32_t fb_w, uint32_t fb_h);

    /* CerfVirtResize calls this (JIT thread) when the guest acks a re-mode;
       marshals to the UI thread to repoint the framebuffer + canvas surface. */
    void NotifyGuestRemoded(uint32_t guest_w, uint32_t guest_h);

    /* Any thread. Marshal a switch to the UART tab to the UI thread (so a guest
       power-down / reboot banner is visible). rearm_framebuffer re-arms the
       framebuffer auto-switch so a rebooting guest's video returns to it. */
    void ShowUartTab(bool rearm_framebuffer);

    /* Any thread. Run `job` on the main UI thread; dropped if the window is
       gone. Lets an action run off its caller's thread (a VGA card window
       ejecting itself must not run the eject on its own UI thread — the eject
       joins that thread). */
    void RunOnUiThread(std::function<void()> job);

    HWND Hwnd() const { return hwnd_; }

    /* HostMenu's "Match guest size" state + action. */
    bool FollowGuest() const { return follow_guest_; }
    void MatchGuestSize();

private:
    void StopUiThread();   /* idempotent: close window + join the UI thread */
    void UiThreadMain();
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    void  AutoResizeToGuest();

    /* "{device_name} • {os} • CERF {ver}" from cerf.json meta; empty meta
       fields are dropped, so an absent meta block yields just "CERF {ver}". */
    std::wstring ComposeWindowTitle() const;

    /* Guest-additions: when adopt-resolution is enabled, size the guest surface
       to the work area of the monitor this window landed on, minus the exact
       window chrome. Runs on the UI thread after the window exists but before
       the canvas is built and before the guest reads its framebuffer dims. */
    void  AdoptResolutionToWindowMonitor();

    /* Non-client + menu + status-bar pixel overhead around a guest surface, at
       the given DPI (Per-Monitor-v2: pass the window's monitor DPI). */
    void  WindowChromeExtent(UINT dpi, int& extra_w, int& extra_h) const;

    /* Size the window so its client holds an sw x sh guest surface, but never
       larger than the work area, and pull the origin in so the whole frame
       stays on-screen. An oversized surface leaves the canvas smaller than the
       surface, which engages the canvas scrollbars. */
    void  FitWindowToSurface(uint32_t sw, uint32_t sh);

    std::thread             ui_thread_;
    std::atomic<bool>       ui_ready_{false};
    std::mutex              ui_ready_mutex_;
    std::condition_variable ui_ready_cv_;

    HWND  hwnd_ = nullptr;

    uint32_t initial_surface_w_ = 0;
    uint32_t initial_surface_h_ = 0;

    bool follow_guest_  = true;   /* false once user resizes/maximizes */
    bool user_resizing_ = false;  /* between WM_ENTER/EXITSIZEMOVE */

    bool      closing_          = false;  /* WM_CLOSE seen; waiting on JIT stop */
    ULONGLONG close_start_tick_ = 0;      /* GetTickCount64 at WM_CLOSE */
};
