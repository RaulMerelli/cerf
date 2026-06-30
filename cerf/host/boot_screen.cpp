#define NOMINMAX

#include "boot_screen.h"

#include "../boards/board_context.h"
#include "../core/cerf_emulator.h"
#include "../core/string_utils.h"
#include "boot_bar.h"
#include "emulation_pause.h"
#include "host_gdiplus.h"

#include <algorithm>
#include <cstring>
#include <objbase.h>
#include <gdiplus.h>

REGISTER_SERVICE(BootScreen);

namespace {

constexpr uint64_t kCerfFadeInMs  = 350;
constexpr uint64_t kCerfHoldMs    = 1200;
constexpr uint64_t kCerfFadeOutMs = 350;
constexpr uint64_t kOemFadeInMs   = 350;
constexpr uint64_t kOemHoldMs     = 2000;

constexpr float    kDimOpacity    = 0.15f;

constexpr int      kOemMaxWidthPx = 190;   /* OEM logos authored <= 190 px wide */
constexpr uint32_t kCerfLogoMinPx = 96;
constexpr uint32_t kCerfLogoMaxPx = 220;

constexpr int      kLabelFontPx      = 18;
constexpr int      kDisclaimerFontPx = 12;
const wchar_t*     kDisclaimer =
    L"Logos are property of their respective owners";
const wchar_t*     kResumeHint =
    L"If nothing happens, try View -> Framebuffer and do some input - maybe screen is dimmed";
const wchar_t*     kResumeStalledHint =
    L"WARNING: framebuffer blank for 10s - the guest may have hung. "
    L"Try View -> Framebuffer and send input if the screen is only dimmed.";

}  /* namespace */

BootScreen::~BootScreen() {
    if (label_font_)      DeleteObject(label_font_);
    if (disclaimer_font_) DeleteObject(disclaimer_font_);
}

void BootScreen::OnShutdown() {
    /* Free GDI+ bitmaps here, not in ~BootScreen: OnShutdown runs before any
       destructor, so HostGdiPlus (GdiplusShutdown in its dtor) is still up.
       Deleting a Bitmap after GdiplusShutdown faults. */
    delete cerf_logo_; cerf_logo_ = nullptr;
    delete oem_logo_;  oem_logo_  = nullptr;
}

void BootScreen::OnReady() {
    auto& bd      = emu_.Get<BoardContext>();
    oem_resource_ = bd.GetBootLogoResource();
    short_name_   = Utf8ToWide(bd.GetShortBoardName());
}

void BootScreen::RenderInto(HDC dc, uint32_t* dib_bgra32,
                            uint32_t width, uint32_t height) {
    std::memset(dib_bgra32, 0, (size_t)width * height * 4u);
    Advance(emu_.Get<EmulationPause>().AnimationTickMs());
    if (!Finished()) DrawAnimation(dc, width, height);
    else             DrawHeldFinal(dc, width, height);
    emu_.Get<BootBar>().RenderInto(dib_bgra32, width, height);
}

void BootScreen::EnsureLogosLoaded() {
    if (logos_loaded_) return;
    logos_loaded_ = true;
    auto& gdip = emu_.Get<HostGdiPlus>();
    cerf_logo_ = gdip.DecodeResourcePng(L"CERF_LOGO");
    oem_logo_  = gdip.DecodeResourcePng(oem_resource_);
}

void BootScreen::EnsureFonts() {
    if (!label_font_)
        label_font_ = CreateFontW(-kLabelFontPx, 0, 0, 0, FW_BOLD, FALSE, FALSE,
                                  FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    if (!disclaimer_font_)
        disclaimer_font_ = CreateFontW(-kDisclaimerFontPx, 0, 0, 0, FW_NORMAL, FALSE,
                                       FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
}

std::wstring BootScreen::CurrentLabelText() const {
    if (fb_latched_)                          return L"Switched to LCD";
    if (label_mode_ == LabelMode::Resuming)   return L"Resuming...";
    if (label_mode_ == LabelMode::Restarting) return L"Restarting...";
    return L"Starting " + short_name_ + L"...";
}

/* In Resuming mode the panel may be dimmed-until-input and the framebuffer tab
   not shown yet; the disclaimer becomes a hint to reach it via View -> Framebuffer. */
const wchar_t* BootScreen::CurrentDisclaimerText() const {
    if (label_mode_ != LabelMode::Resuming) return kDisclaimer;
    return resume_stalled_ ? kResumeStalledHint : kResumeHint;
}

bool BootScreen::IsResuming() const {
    return label_mode_ == LabelMode::Resuming && !fb_latched_;
}

void BootScreen::SetResumeStalled() { resume_stalled_ = true; }

void BootScreen::Advance(uint64_t now) {
    if (!started_) { started_ = true; phase_ = Phase::CerfFadeIn; phase_start_ = now; }

    if (restart_req_.exchange(false)) {
        label_mode_  = restart_resuming_.load(std::memory_order_acquire)
                           ? LabelMode::Resuming : LabelMode::Restarting;
        fb_latched_  = false;
        /* Drop a latch request the previous session left pending (Advance stops
           once the framebuffer tab takes over, so it was never consumed); else
           it fires this same Advance and flips the fresh label to "Switched to
           LCD". The resumed guest's first frame re-latches later. */
        fb_latched_req_.store(false, std::memory_order_release);
        resume_stalled_ = false;
        phase_       = Phase::OemFadeIn;
        phase_start_ = now;
    }
    if (fb_latched_req_.exchange(false)) { fb_latched_ = true; phase_ = Phase::Finished; }

    const uint64_t elapsed = (now >= phase_start_) ? (now - phase_start_) : 0;

    switch (phase_) {
        case Phase::CerfFadeIn:
            cur_op_ = std::min(1.0f, (float)elapsed / (float)kCerfFadeInMs);
            if (elapsed >= kCerfFadeInMs) { phase_ = Phase::CerfHold; phase_start_ = now; }
            break;
        case Phase::CerfHold:
            cur_op_ = 1.0f;
            if (elapsed >= kCerfHoldMs) { phase_ = Phase::CerfFadeOut; phase_start_ = now; }
            break;
        case Phase::CerfFadeOut:
            cur_op_ = std::max(0.0f, 1.0f - (float)elapsed / (float)kCerfFadeOutMs);
            if (elapsed >= kCerfFadeOutMs) { phase_ = Phase::OemFadeIn; phase_start_ = now; }
            break;
        case Phase::OemFadeIn:
            cur_op_ = std::min(1.0f, (float)elapsed / (float)kOemFadeInMs);
            if (elapsed >= kOemFadeInMs) { phase_ = Phase::OemHold; phase_start_ = now; }
            break;
        case Phase::OemHold:
            cur_op_ = 1.0f;
            if (elapsed >= kOemHoldMs) phase_ = Phase::Finished;
            break;
        case Phase::Finished:
            cur_op_ = 1.0f;
            break;
    }
}

void BootScreen::DrawLogoFrame(HDC dc, uint32_t width, uint32_t height,
                               bool use_oem, float opacity,
                               bool show_label, const std::wstring& label,
                               bool show_disclaimer, bool cerf_native_size) {
    EnsureLogosLoaded();
    opacity = std::clamp(opacity, 0.0f, 1.0f);

    Gdiplus::Bitmap* bmp = use_oem ? oem_logo_ : cerf_logo_;
    if (!bmp) bmp = cerf_logo_;   /* OEM decode failed -> CERF fallback */

    int dst_w = 0, dst_h = 0;
    if (bmp) {
        const int bw = (int)bmp->GetWidth();
        const int bh = (int)bmp->GetHeight();
        if (use_oem && oem_logo_) {
            const int cap_w = std::min((int)kOemMaxWidthPx, std::max(1, (int)width - 20));
            const int cap_h = std::max(1, (int)(height * 0.45f));
            float s = 1.0f;
            if (bw > cap_w) s = (float)cap_w / (float)bw;
            if (bh * s > cap_h) s = (float)cap_h / (float)bh;
            dst_w = std::max(1, (int)(bw * s));
            dst_h = std::max(1, (int)(bh * s));
        } else if (cerf_native_size) {
            dst_w = bw;   /* native size, window-independent (text-mode watermark) */
            dst_h = bh;
        } else {
            const uint32_t md = std::min(width, height);
            const int sz = (int)std::clamp(md / 3u, kCerfLogoMinPx, kCerfLogoMaxPx);
            dst_w = dst_h = sz;
        }
    }

    /* With a label below (OEM phase / held), the logo sits above centre to
       leave room; without one (CERF intro, text-mode watermark) it is centred
       on the screen. */
    const int cx = (int)width / 2;
    const int cy = show_label ? (int)(height * 0.40f) : (int)height / 2;

    if (bmp && dst_w > 0 && dst_h > 0) {
        Gdiplus::Graphics g(dc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

        Gdiplus::ColorMatrix cm = {};
        cm.m[0][0] = cm.m[1][1] = cm.m[2][2] = 1.0f;
        cm.m[3][3] = opacity;
        cm.m[4][4] = 1.0f;
        Gdiplus::ImageAttributes ia;
        ia.SetColorMatrix(&cm);

        Gdiplus::Rect dst(cx - dst_w / 2, cy - dst_h / 2, dst_w, dst_h);
        g.DrawImage(bmp, dst, 0, 0, (int)bmp->GetWidth(), (int)bmp->GetHeight(),
                    Gdiplus::UnitPixel, &ia);
    }   /* g flushes to the DC on scope exit, before the GDI text below */

    if (!show_label && !show_disclaimer) return;

    EnsureFonts();
    const int v = (int)(255.0f * opacity);
    SetBkMode(dc, TRANSPARENT);

    /* Lay the disclaimer out first (bottom-anchored, above the 16 px boot bar)
       so the label can be kept above it on short panels (e.g. 640x240). */
    constexpr int kMargin = 8;
    int disclaimer_top = (int)height;   /* sentinel: nothing below the label */
    RECT disc_rect{};
    if (show_disclaimer && disclaimer_font_) {
        HFONT old = (HFONT)SelectObject(dc, disclaimer_font_);
        RECT calc{ kMargin, 0, (int)width - kMargin, 0 };
        DrawTextW(dc, CurrentDisclaimerText(), -1, &calc,
                  DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
        const int dh     = calc.bottom - calc.top;
        const int bottom = (int)height - 16 - 6;
        disclaimer_top = bottom - dh;
        disc_rect = RECT{ kMargin, disclaimer_top, (int)width - kMargin, bottom };
        SelectObject(dc, old);
    }

    if (show_label && label_font_) {
        HFONT old = (HFONT)SelectObject(dc, label_font_);
        SetTextColor(dc, RGB(v, v, v));
        int label_y = cy + dst_h / 2 + 44;
        const int max_y = disclaimer_top - kLabelFontPx - 8;   /* keep clear of disclaimer */
        if (label_y > max_y)                  label_y = max_y;
        if (label_y < cy + dst_h / 2 + 8)     label_y = cy + dst_h / 2 + 8;  /* but below logo */
        RECT r{ 0, label_y, (int)width, label_y + kLabelFontPx * 2 };
        DrawTextW(dc, label.c_str(), (int)label.size(), &r,
                  DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old);
    }
    if (show_disclaimer && disclaimer_font_) {
        const int dv = (int)(150.0f * opacity);
        HFONT old = (HFONT)SelectObject(dc, disclaimer_font_);
        SetTextColor(dc, RGB(dv, dv, dv));
        DrawTextW(dc, CurrentDisclaimerText(), -1, &disc_rect,
                  DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
    }
}

void BootScreen::DrawAnimation(HDC dc, uint32_t width, uint32_t height) {
    switch (phase_) {
        case Phase::CerfFadeIn:
        case Phase::CerfHold:
        case Phase::CerfFadeOut:
            DrawLogoFrame(dc, width, height, /*use_oem=*/false, cur_op_,
                          /*label=*/false, L"", /*disclaimer=*/false);
            break;
        case Phase::OemFadeIn:
        case Phase::OemHold: {
            const bool oem = (oem_logo_ != nullptr);
            DrawLogoFrame(dc, width, height, oem, cur_op_,
                          /*label=*/true, CurrentLabelText(),
                          /*disclaimer=*/(oem || label_mode_ == LabelMode::Resuming));
            break;
        }
        case Phase::Finished:
            break;
    }
}

void BootScreen::DrawHeldFinal(HDC dc, uint32_t width, uint32_t height) {
    const bool oem = (oem_logo_ != nullptr);
    DrawLogoFrame(dc, width, height, oem, 1.0f,
                  /*label=*/true, CurrentLabelText(),
                  /*disclaimer=*/(oem || label_mode_ == LabelMode::Resuming));
}

void BootScreen::DrawWatermark(HDC dc, uint32_t width, uint32_t height) {
    DrawLogoFrame(dc, width, height, /*use_oem=*/false, kDimOpacity,
                  /*label=*/false, L"", /*disclaimer=*/false,
                  /*cerf_native_size=*/true);
}

void BootScreen::Restart(bool resuming) {
    restart_resuming_.store(resuming, std::memory_order_release);
    restart_req_.store(true, std::memory_order_release);
}
void BootScreen::OnFramebufferLatched() { fb_latched_req_.store(true); }
