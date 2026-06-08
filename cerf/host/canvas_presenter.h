#pragma once

#define NOMINMAX
#include <windows.h>

#include "frame_source.h"

#include <atomic>
#include <cstdint>
#include <vector>

/* The shared guest-surface presentation core: viewport mode (Original /
   Aspect / Stretch), optional antialiased scaling, Original-mode scrollbars,
   and the single host-pixel -> guest-surface coordinate transform. One
   instance per window — the main HostCanvas owns one (fed by the board
   FrameRenderer), and each PCMCIA VGA card window owns one (fed by the
   card's own producer), so the scale/stretch View semantics are defined
   exactly once and behave identically in every window.

   The presenter owns the guest-surface DIB (sized to the producer's native
   frame) and composes it, scaled per the viewport mode, into a present
   surface (the window backbuffer) the OWNER provides — the owner keeps the
   present DIB because on the main window it is shared with the UART /
   MemoryVisualizer tabs. The presenter does NOT own a window, a timer, or a
   message loop; the owner forwards the relevant messages. */
class CanvasPresenter {
public:
    enum class ViewportMode { Original, Aspect, Stretch };

    /* source may be null — the main canvas binds the board FrameRenderer via
       TryGet, which yields null on a board with no renderer (matching the
       prior HostCanvas behavior). A VGA card always passes its own producer. */
    explicit CanvasPresenter(FrameSource* source) : source_(source) {}
    ~CanvasPresenter();

    /* Late-bind the producer when it isn't known at presenter construction
       (HostCanvas resolves the board FrameRenderer only once its window is
       created). Bound once, before the first ComposeInto. */
    void SetSource(FrameSource* source) { source_ = source; }

    /* Whether the producer currently has a frame — drives the owner's
       auto-switch-to-framebuffer decision, independent of what is composed. */
    bool SourceHasFrame() const { return source_ && source_->HasFrame(); }

    CanvasPresenter(const CanvasPresenter&)            = delete;
    CanvasPresenter& operator=(const CanvasPresenter&) = delete;

    /* UI thread. Bind to the owner's window and build the guest DIB at the
       producer's native surface size. Call once the window exists. */
    void Attach(HWND hwnd, uint32_t surf_w, uint32_t surf_h);
    /* UI thread. Release the guest DIB/DC (owner's WM_DESTROY). */
    void Detach();

    /* UI thread. The present surface (window client) changed size. */
    void OnCanvasResized(int canvas_w, int canvas_h);
    /* UI thread. The producer's native frame size changed (rebuilds the
       guest DIB). */
    void SetSurfaceSize(uint32_t w, uint32_t h);

    /* Native guest-surface dimensions — what the producer draws at and the
       coordinate span HostToGuest maps against. Atomic so an off-thread
       touch sampler can read them. */
    uint32_t SurfaceWidth () const { return surface_w_.load(std::memory_order_acquire); }
    uint32_t SurfaceHeight() const { return surface_h_.load(std::memory_order_acquire); }

    ViewportMode Mode()      const { return mode_; }
    bool         Antialias() const { return antialias_; }
    void SetViewportMode(ViewportMode m);
    void SetAntialias(bool on);

    /* UI thread. Pull a fresh frame from the producer (if any) and compose it,
       scaled per the viewport mode, into the owner's present surface. Returns
       true when a frame was drawn, false when the producer has none (the owner
       decides what to paint instead). present_bits is the owner's backbuffer,
       sized to the last OnCanvasResized dimensions (BGRA32); present_dc wraps
       it. */
    bool ComposeInto(HDC present_dc, uint32_t* present_bits);

    /* UI thread. Whether the presenter's content is the one currently shown
       in the owner window. The main HostCanvas sets false while a non-
       framebuffer tab is up (suppressing scrollbars); a VGA card window is
       always active. */
    void SetActive(bool active);

    /* UI thread. Scrollbar plumbing for Original mode (no-op in Aspect /
       Stretch, or while inactive). */
    void UpdateScrollbars();
    void OnHScroll(WPARAM wp);
    void OnVScroll(WPARAM wp);

    /* canvas-client (cx,cy) -> guest-surface (sx,sy). False when the point is
       outside the blitted guest image. */
    bool HostToGuest(int cx, int cy, int& sx, int& sy) const;
    void ClampGuest(int& sx, int& sy) const;

    /* UI thread. Render the live frame fresh and copy it out 1:1 for
       screenshot/clipboard. False if the producer has no frame. */
    bool CaptureSurface(std::vector<uint32_t>& out, uint32_t& w, uint32_t& h);

private:
    void RebuildGuestDib(uint32_t w, uint32_t h);

    /* Both ComposeInto and HostToGuest derive from this one Layout — separate
       math would land taps off the rendered image whenever the two drift. */
    struct Layout {
        int  dst_x, dst_y, dst_w, dst_h;
        int  src_x, src_y, src_w, src_h;
        bool stretch;
    };
    Layout ComputeLayout(int canvas_w, int canvas_h) const;

    FrameSource* source_;

    HWND      hwnd_      = nullptr;
    HDC       guest_dc_  = nullptr;   /* wraps guest_dib_ (surface-sized) */
    HBITMAP   guest_dib_ = nullptr;
    uint32_t* guest_bits_ = nullptr;

    int canvas_w_ = 0;
    int canvas_h_ = 0;
    std::atomic<uint32_t> surface_w_{0};
    std::atomic<uint32_t> surface_h_{0};

    ViewportMode mode_      = ViewportMode::Original;
    bool         antialias_ = false;  /* off = crisp nearest-neighbor scale */
    bool         active_    = true;   /* owner content currently shown */

    int  scroll_x_  = 0;
    int  scroll_y_  = 0;
    bool hsb_shown_ = false;
    bool vsb_shown_ = false;
};
