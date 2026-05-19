#define NOMINMAX

#include "host_window.h"

#include <dwmapi.h>

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../jit/jit_runner.h"
#include "frame_renderer.h"
#include "keyboard_input.h"
#include "lcd_scan_tick.h"
#include "touch_input.h"
#include "uart_screen.h"

REGISTER_SERVICE(HostWindow);

namespace {

constexpr wchar_t  kWindowClass[]      = L"CerfHostWindow";
constexpr UINT_PTR kPresentTimerId     = 1;
constexpr UINT     kPresentIntervalMs  = 16;  /* ~60 Hz */

}  /* namespace */

HostWindow::~HostWindow() {
    if (ui_thread_.joinable()) {
        if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        ui_thread_.join();
    }
}

void HostWindow::OnReady() {
    const uint32_t initial_w = emu_.Config().start_window_width;
    const uint32_t initial_h = emu_.Config().start_window_height;
    width_ .store(initial_w, std::memory_order_release);
    height_.store(initial_h, std::memory_order_release);
    LOG(Lcd, "HostWindow OnReady: opening at %ux%u (boot splash)\n",
        initial_w, initial_h);

    /* Make the host window pixel-exact regardless of system DPI scaling. */
    SetProcessDPIAware();

    ui_thread_ = std::thread([this] { UiThreadMain(); });

    /* Wait for the window to be up so the caller can immediately
       resolve us and proceed without races on hwnd_ / dib_bits_. */
    std::unique_lock<std::mutex> lk(ui_ready_mutex_);
    ui_ready_cv_.wait(lk, [&] { return ui_ready_.load(); });
}

void HostWindow::OnLcdEnabled(uint32_t fb_w, uint32_t fb_h) {
    uint32_t host_w = fb_w;
    uint32_t host_h = fb_h;
    if (auto* fr = emu_.TryGet<FrameRenderer>()) {
        const auto [w, h] = fr->HostSizeFor(fb_w, fb_h);
        host_w = w;
        host_h = h;
    }
    pending_w_.store(host_w, std::memory_order_release);
    pending_h_.store(host_h, std::memory_order_release);
    LOG(Lcd, "HostWindow::OnLcdEnabled: fb=%ux%u host=%ux%u (pending)\n",
        fb_w, fb_h, host_w, host_h);
}

void HostWindow::UiThreadMain() {
    /* Win32 window classes are process-wide. Each HostWindow instance
       attempts the registration; ERROR_CLASS_ALREADY_EXISTS from a
       sibling instance is the success case for everyone after the
       first. */
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &HostWindow::WndProcStatic;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIconW(GetModuleHandleW(nullptr),
                                 MAKEINTRESOURCEW(1));
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClass;
    if (RegisterClassExW(&wc) == 0) {
        const DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            LOG(Caution, "HostWindow: RegisterClassExW failed "
                    "(gle=%lu)\n", err);
            CerfFatalExit(1);
        }
    }

    const uint32_t initial_w = width_ .load(std::memory_order_acquire);
    const uint32_t initial_h = height_.load(std::memory_order_acquire);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = { 0, 0, (LONG)initial_w, (LONG)initial_h };
    AdjustWindowRect(&r, style, FALSE);

    hwnd_ = CreateWindowExW(0, kWindowClass, L"CERF",
                            style, CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top,
                            nullptr, nullptr,
                            GetModuleHandleW(nullptr), this);
    if (!hwnd_) {
        LOG(Caution, "HostWindow: CreateWindowExW failed (gle=%lu)\n",
                GetLastError());
        CerfFatalExit(1);
    }

    /* DO NOT drop the 19 fallback — Windows 10 1809-1909 only
       accept the predecessor attribute; 20 is 20H1+. */
    {
        const BOOL dark = TRUE;
        if (FAILED(DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark)))) {
            DwmSetWindowAttribute(hwnd_, 19, &dark, sizeof(dark));
        }
    }

    /* Top-down BGRA32 DIB section as the present scratch. CreateDIBSection
       gives us a host pointer (dib_bits_) the renderers write directly;
       mem_dc_ wraps the same memory for GDI text/icon drawing through
       UartScreen. */
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth       = (LONG)initial_w;
    bmi.bmiHeader.biHeight      = -(LONG)initial_h;  /* negative = top-down */
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screen_dc = GetDC(hwnd_);
    void* bits = nullptr;
    dib_ = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(hwnd_, screen_dc);
    if (!dib_ || !bits) {
        LOG(Caution, "HostWindow: CreateDIBSection failed (gle=%lu)\n",
                GetLastError());
        CerfFatalExit(1);
    }
    dib_bits_ = static_cast<uint32_t*>(bits);

    mem_dc_ = CreateCompatibleDC(nullptr);
    SelectObject(mem_dc_, dib_);

    /* First render before the timer kicks — picks up UartScreen
       state A immediately so the window opens with the splash logo
       rather than a black frame. */
    TickAndPresent();

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    timer_id_ = SetTimer(hwnd_, kPresentTimerId, kPresentIntervalMs, nullptr);

    {
        std::lock_guard<std::mutex> lk(ui_ready_mutex_);
        ui_ready_.store(true);
    }
    ui_ready_cv_.notify_all();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (timer_id_) { KillTimer(hwnd_, timer_id_); timer_id_ = 0; }
    if (mem_dc_)   { DeleteDC(mem_dc_); mem_dc_ = nullptr; }
    if (dib_)      { DeleteObject(dib_); dib_ = nullptr; dib_bits_ = nullptr; }

    close_requested_.store(true);
    LOG(Lcd, "HostWindow UI thread exiting\n");
}

LRESULT CALLBACK HostWindow::WndProcStatic(HWND hwnd, UINT msg,
                                           WPARAM wp, LPARAM lp) {
    HostWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<HostWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<HostWindow*>(
                   GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->WndProc(hwnd, msg, wp, lp)
                : DefWindowProcW(hwnd, msg, wp, lp);
}

void HostWindow::ResizeDibAndWindow(uint32_t new_w, uint32_t new_h) {
    HBITMAP old_dib = dib_;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth       = (LONG)new_w;
    bmi.bmiHeader.biHeight      = -(LONG)new_h;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screen_dc = GetDC(hwnd_);
    void* bits = nullptr;
    HBITMAP new_dib = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS,
                                       &bits, nullptr, 0);
    ReleaseDC(hwnd_, screen_dc);
    if (!new_dib || !bits) {
        LOG(Caution, "HostWindow::ResizeDibAndWindow: CreateDIBSection "
                "failed for %ux%u (gle=%lu); keeping %ux%u\n",
            new_w, new_h, GetLastError(),
            width_.load(std::memory_order_acquire),
            height_.load(std::memory_order_acquire));
        if (new_dib) DeleteObject(new_dib);
        return;
    }

    SelectObject(mem_dc_, new_dib);
    DeleteObject(old_dib);
    dib_      = new_dib;
    dib_bits_ = static_cast<uint32_t*>(bits);
    width_ .store(new_w, std::memory_order_release);
    height_.store(new_h, std::memory_order_release);

    const DWORD style = (DWORD)GetWindowLongW(hwnd_, GWL_STYLE);
    RECT r = { 0, 0, (LONG)new_w, (LONG)new_h };
    AdjustWindowRect(&r, style, FALSE);
    SetWindowPos(hwnd_, nullptr, 0, 0,
                 r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOMOVE);
    LOG(Lcd, "HostWindow resized to %ux%u\n", new_w, new_h);
}

void HostWindow::TickAndPresent() {
    /* Drive SoC scan state (VSYNC bit / GO auto-clear / FRAMEDONE
       sequencing) before HasFrame() so the renderer queries fresh
       state on this present cycle. */
    if (auto* tick = emu_.TryGet<LcdScanTick>()) {
        tick->OnHostTick();
    }

    auto* fr = emu_.TryGet<FrameRenderer>();
    const bool has_frame = fr && fr->HasFrame();

    if (has_frame && last_renderer_ != LastRenderer::Frame) {
        const uint32_t new_w = pending_w_.load(std::memory_order_acquire);
        const uint32_t new_h = pending_h_.load(std::memory_order_acquire);
        if (new_w != 0 && new_h != 0
            && (new_w != width_ .load(std::memory_order_acquire)
             || new_h != height_.load(std::memory_order_acquire))) {
            ResizeDibAndWindow(new_w, new_h);
        }
    }

    const uint32_t w = width_ .load(std::memory_order_acquire);
    const uint32_t h = height_.load(std::memory_order_acquire);
    if (has_frame) {
        last_renderer_ = LastRenderer::Frame;
        fr->RenderInto(dib_bits_, w, h);
    } else {
        last_renderer_ = LastRenderer::Uart;
        emu_.Get<UartScreen>().RenderInto(mem_dc_, dib_bits_, w, h);
    }
}

LRESULT HostWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            if (wp == kPresentTimerId) {
                TickAndPresent();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            const int w = (int)width_ .load(std::memory_order_acquire);
            const int h = (int)height_.load(std::memory_order_acquire);
            BitBlt(dc, 0, 0, w, h, mem_dc_, 0, 0, SRCCOPY);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;  /* we paint the entire client area in WM_PAINT */

        case WM_KEYDOWN:
        case WM_KEYUP: {
            if (auto* kbd = emu_.TryGet<KeyboardInput>()) {
                kbd->OnHostKey(static_cast<uint8_t>(wp),
                               /*key_up=*/ msg == WM_KEYUP);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            if (auto* t = emu_.TryGet<TouchInput>()) {
                t->OnPenDown((int)(short)LOWORD(lp),
                             (int)(short)HIWORD(lp));
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (auto* t = emu_.TryGet<TouchInput>()) {
                t->OnPenMove((int)(short)LOWORD(lp),
                             (int)(short)HIWORD(lp));
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (auto* t = emu_.TryGet<TouchInput>()) {
                t->OnPenUp((int)(short)LOWORD(lp),
                           (int)(short)HIWORD(lp));
            }
            return 0;
        }
        case WM_CAPTURECHANGED: {
            if (auto* t = emu_.TryGet<TouchInput>()) {
                t->OnCaptureLost();
            }
            return 0;
        }

        case WM_CLOSE:
            /* Without RequestStop, the JIT thread keeps grinding ARM
               code after the HWND is destroyed and orphans cerf.exe. */
            close_requested_.store(true);
            if (auto* jit = emu_.TryGet<JitRunner>()) jit->RequestStop();
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
