#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace Gdiplus { class Bitmap; }

/* The Boot Screen tab: CERF logo fade animation + bottom CPU-activity bar.
   Advance() runs off the render loop's wall clock, never an internal timer: a
   separate timer would not freeze on pause and would keep ticking after the
   tab stops being rendered. */
class BootScreen : public Service {
public:
    using Service::Service;
    ~BootScreen() override;

    void OnReady() override;
    void OnShutdown() override;

    /* UI thread. Compose the Boot Screen tab into the host surface: advance the
       animation to the present clock, draw the current logo state, then the
       activity bar. */
    void RenderInto(HDC dc, uint32_t* dib_bgra32, uint32_t width, uint32_t height);

    /* UI thread. Draw the dimmed, native-size CERF logo centred on `dc` as a
       static watermark (no animation). The Hardware Screen tab draws it behind
       its text. */
    void DrawWatermark(HDC dc, uint32_t width, uint32_t height);

    bool Finished() const { return phase_ == Phase::Finished; }

    /* Any thread. Restart the animation from the text fade-in (CERF logo stays
       held). resuming picks the label: "Resuming..." (deep-sleep wake) vs
       "Restarting..." (guest reboot). */
    void Restart(bool resuming = false);

    /* Any thread. The framebuffer tab has taken over; finish the animation and
       switch the held label to "Switched to LCD". */
    void OnFramebufferLatched();

    /* UI thread. IsResuming: resume screen up, framebuffer not yet latched.
       SetResumeStalled: flip the resume hint to a blank-framebuffer warning. */
    bool IsResuming() const;
    void SetResumeStalled();

private:
    enum class Phase { CerfFadeIn, CerfHold, TextFadeIn, TextHold, Finished };
    enum class LabelMode { Starting, Restarting, Resuming };

    /* UI thread. Progress the state machine to wall-clock `now_ms` and consume
       any pending cross-thread requests (restart / framebuffer-latch). */
    void         Advance(uint64_t now_ms);
    void         EnsureLogosLoaded();
    void         EnsureFonts();
    std::wstring CurrentLabelText() const;
    const wchar_t* CurrentDisclaimerText() const;
    void DrawLogoFrame(HDC dc, uint32_t width, uint32_t height,
                       float logo_opacity,
                       bool show_label, const std::wstring& label, float text_opacity,
                       bool show_disclaimer, bool cerf_native_size = false);
    void DrawAnimation(HDC dc, uint32_t width, uint32_t height);   /* live frame */
    void DrawHeldFinal(HDC dc, uint32_t width, uint32_t height);   /* finished */

    Gdiplus::Bitmap* cerf_logo_     = nullptr;
    bool             logos_loaded_  = false;
    std::wstring     short_name_;               /* board short name, widened */

    HFONT label_font_      = nullptr;
    HFONT disclaimer_font_ = nullptr;

    /* State-machine fields - touched only by Advance/Draw* on the UI thread. */
    Phase     phase_        = Phase::CerfFadeIn;
    uint64_t  phase_start_  = 0;
    bool      started_      = false;
    float     cur_op_       = 0.0f;
    LabelMode label_mode_   = LabelMode::Starting;
    bool      fb_latched_   = false;
    bool      resume_stalled_ = false;

    /* Cross-thread requests, consumed at the top of Advance. */
    std::atomic<bool> restart_req_{false};
    std::atomic<bool> restart_resuming_{false};  /* label: Resuming vs Restarting */
    std::atomic<bool> fb_latched_req_{false};
};
