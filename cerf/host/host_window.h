#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

class HostWindow : public Service {
public:
    using Service::Service;
    ~HostWindow() override;
    void OnReady() override;

    bool IsClosed() const { return close_requested_.load(); }

    /* SoC LCD service calls on guest panel-enable edge. fb_(w|h) are
       raw LCD-controller dimensions; HostWindow runs them through the
       current FrameRenderer's HostSizeFor so rotating renderers can
       swap. */
    void OnLcdEnabled(uint32_t fb_w, uint32_t fb_h);

    uint32_t ClientWidth () const { return width_.load(std::memory_order_acquire); }
    uint32_t ClientHeight() const { return height_.load(std::memory_order_acquire); }

private:
    void UiThreadMain();
    void TickAndPresent();
    void ResizeDibAndWindow(uint32_t new_w, uint32_t new_h);

    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    std::thread             ui_thread_;
    std::atomic<bool>       close_requested_{false};
    std::atomic<bool>       ui_ready_{false};
    std::mutex              ui_ready_mutex_;
    std::condition_variable ui_ready_cv_;

    HWND      hwnd_     = nullptr;
    HDC       mem_dc_   = nullptr;
    HBITMAP   dib_      = nullptr;
    uint32_t* dib_bits_ = nullptr;
    UINT_PTR  timer_id_ = 0;

    std::atomic<uint32_t> width_ {0};
    std::atomic<uint32_t> height_{0};

    std::atomic<uint32_t> pending_w_{0};
    std::atomic<uint32_t> pending_h_{0};

    enum class LastRenderer { None, Uart, Frame };
    LastRenderer last_renderer_ = LastRenderer::None;
};
