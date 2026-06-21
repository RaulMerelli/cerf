#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace Gdiplus { class Bitmap; }

/* The HwScreen boot-visual owner: CERF/OEM logo fade animation and the held /
   dimmed final states. Advance() must be driven by the render loop's wall clock
   (no internal thread/timer): a separate timer would not freeze on pause and
   would keep running after the framebuffer tab stops calling RenderInto. */
class HwBootAnimation : public Service {
public:
    using Service::Service;
    ~HwBootAnimation() override;

    void OnReady() override;

    /* UI thread. Progress the state machine to wall-clock `now_ms` and consume
       any pending cross-thread requests (restart / abort / framebuffer-latch). */
    void Advance(uint64_t now_ms);

    bool Finished() const { return phase_ == Phase::Finished; }

    /* False when --boot-anim=disable (default in dev builds): no logo intro;
       the screen goes straight to text/held state. */
    bool Enabled() const { return enabled_; }

    /* UI thread, all drawing into HostWindow's present DC. */
    void DrawInto(HDC dc, uint32_t width, uint32_t height);          /* live frame */
    void DrawHeldFinal(HDC dc, uint32_t width, uint32_t height);     /* finished, no TX */
    void DrawDimmedCenterLogo(HDC dc, uint32_t width, uint32_t height); /* text-mode bg */

    /* Any thread. Restart the animation from the OEM-logo fade-in. resuming
       picks the label: "Resuming..." (deep-sleep wake) vs "Restarting..."
       (guest reboot). */
    void Restart(bool resuming = false);

    /* Any thread. Stop the animation immediately and jump to the finished state
       (hibernation restore failure prints text and awaits a keypress). */
    void Abort();

    /* Any thread. The framebuffer tab has taken over; finish the animation and
       switch the held label to "Switched to LCD". */
    void OnFramebufferLatched();

    /* UI thread. IsResuming: resume screen up, framebuffer not yet latched.
       SetResumeStalled: flip the resume hint to a blank-framebuffer warning. */
    bool IsResuming() const;
    void SetResumeStalled();

private:
    enum class Phase { CerfFadeIn, CerfHold, CerfFadeOut, OemFadeIn, OemHold, Finished };
    enum class LabelMode { Starting, Restarting, Resuming };

    void         EnsureLogosLoaded();
    void         EnsureFonts();
    std::wstring CurrentLabelText() const;
    const wchar_t* CurrentDisclaimerText() const;
    void DrawLogoFrame(HDC dc, uint32_t width, uint32_t height,
                       bool use_oem, float opacity,
                       bool show_label, const std::wstring& label,
                       bool show_disclaimer,
                       bool cerf_native_size = false);

    Gdiplus::Bitmap* cerf_logo_     = nullptr;
    Gdiplus::Bitmap* oem_logo_      = nullptr;
    bool             logos_loaded_  = false;
    const wchar_t*   oem_resource_  = nullptr;  /* resolved in OnReady */
    std::wstring     short_name_;               /* board short name, widened */
    bool             enabled_       = true;     /* --boot-anim, resolved in OnReady */

    HFONT label_font_      = nullptr;
    HFONT disclaimer_font_ = nullptr;

    /* State-machine fields - touched only by Advance/Draw* on the UI thread. */
    Phase     phase_        = Phase::CerfFadeIn;
    uint64_t  phase_start_  = 0;
    bool      started_      = false;
    float     cur_op_       = 0.0f;
    LabelMode label_mode_   = LabelMode::Starting;
    bool      entered_oem_  = false;  /* an OEM-phase fade-in was reached */
    bool      fb_latched_   = false;
    bool      resume_stalled_ = false;

    /* Cross-thread requests, consumed at the top of Advance. */
    std::atomic<bool> restart_req_{false};
    std::atomic<bool> restart_resuming_{false};  /* label: Resuming vs Restarting */
    std::atomic<bool> abort_req_{false};
    std::atomic<bool> fb_latched_req_{false};
};
