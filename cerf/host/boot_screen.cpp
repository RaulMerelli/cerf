#define NOMINMAX

#include "boot_screen.h"

#include "../boards/board_context.h"
#include "../core/cerf_emulator.h"
#include "../core/string_utils.h"
#include "boot_bar.h"
#include "emulation_pause.h"
#include "host_gdiplus.h"
#include "hw_screen.h"

#include <algorithm>
#include <cstring>
#include <objbase.h>
#include <gdiplus.h>

REGISTER_SERVICE(BootScreen);

namespace {

constexpr uint64_t kCerfFadeInMs  = 350;
constexpr uint64_t kCerfHoldMs    = 1200;
constexpr uint64_t kTextFadeInMs  = 350;
constexpr uint64_t kTextHoldMs    = 2000;

constexpr float    kDimOpacity    = 0.15f;

constexpr uint32_t kCerfLogoMinPx = 96;
constexpr uint32_t kCerfLogoMaxPx = 220;

constexpr int      kLabelFontPx      = 18;
constexpr int      kDisclaimerFontPx = 12;
constexpr int      kMargin           = 8;
constexpr int      kBootBarPx        = 16;
constexpr int      kHwLineHeightPx   = kDisclaimerFontPx + 4;

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
}

void BootScreen::OnReady() {
    short_name_ = Utf8ToWide(emu_.Get<BoardContext>().GetShortBoardName());
}

void BootScreen::RenderInto(HDC dc, uint32_t* dib_bgra32,
                            uint32_t width, uint32_t height) {
    std::memset(dib_bgra32, 0, (size_t)width * height * 4u);
    Advance(emu_.Get<EmulationPause>().AnimationTickMs());
    if (!Finished()) DrawAnimation(dc, width, height);
    else             DrawHeldFinal(dc, width, height);
    DrawHwStatusLine(dc, width, height);
    emu_.Get<BootBar>().RenderInto(dib_bgra32, width, height);
}

void BootScreen::EnsureLogosLoaded() {
    if (logos_loaded_) return;
    logos_loaded_ = true;
    cerf_logo_ = emu_.Get<HostGdiPlus>().DecodeResourcePng(L"CERF_LOGO");
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
    if (fb_latched_)                          return L"LCD is rendering.";
    if (label_mode_ == LabelMode::Restarting) return L"Rebooting...";
    return L"Booting " + short_name_ + L"...";
}

void BootScreen::Advance(uint64_t now) {
    if (!started_) { started_ = true; phase_ = Phase::CerfFadeIn; phase_start_ = now; }

    if (restart_req_.exchange(false)) {
        label_mode_  = LabelMode::Restarting;
        fb_latched_  = false;
        /* Drop a latch request the previous session left pending (Advance stops
           once the framebuffer tab takes over, so it was never consumed); else
           it fires this same Advance and flips the fresh label to "Switched to
           LCD". The resumed guest's first frame re-latches later. */
        fb_latched_req_.store(false, std::memory_order_release);
        phase_       = Phase::TextFadeIn;
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
            if (elapsed >= kCerfHoldMs) {
                phase_ = Phase::TextFadeIn; phase_start_ = now; cur_op_ = 0.0f;
            }
            break;
        case Phase::TextFadeIn:
            cur_op_ = std::min(1.0f, (float)elapsed / (float)kTextFadeInMs);
            if (elapsed >= kTextFadeInMs) { phase_ = Phase::TextHold; phase_start_ = now; }
            break;
        case Phase::TextHold:
            cur_op_ = 1.0f;
            if (elapsed >= kTextHoldMs) phase_ = Phase::Finished;
            break;
        case Phase::Finished:
            cur_op_ = 1.0f;
            break;
    }
}

void BootScreen::DrawLogoFrame(HDC dc, uint32_t width, uint32_t height,
                               float logo_opacity,
                               bool show_label, const std::wstring& label, float text_opacity,
                               bool cerf_native_size) {
    EnsureLogosLoaded();
    logo_opacity = std::clamp(logo_opacity, 0.0f, 1.0f);
    text_opacity = std::clamp(text_opacity, 0.0f, 1.0f);

    Gdiplus::Bitmap* bmp = cerf_logo_;

    int dst_w = 0, dst_h = 0;
    if (bmp) {
        const int bw = (int)bmp->GetWidth();
        const int bh = (int)bmp->GetHeight();
        if (cerf_native_size) {
            dst_w = bw;   /* native size, window-independent (text-mode watermark) */
            dst_h = bh;
        } else {
            const uint32_t md = std::min(width, height);
            const int sz = (int)std::clamp(md / 3u, kCerfLogoMinPx, kCerfLogoMaxPx);
            dst_w = dst_h = sz;
        }
    }

    /* With a label below (text phase / held), the logo sits above centre to
       leave room; without one (CERF intro, text-mode watermark) it is centred
       on the screen. */
    const int cx = (int)width / 2;
    const float cy_frac = show_label ? (0.5f - 0.10f * text_opacity) : 0.5f;
    const int cy = (int)(height * cy_frac);

    if (bmp && dst_w > 0 && dst_h > 0) {
        Gdiplus::Graphics g(dc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

        Gdiplus::ColorMatrix cm = {};
        cm.m[0][0] = cm.m[1][1] = cm.m[2][2] = 1.0f;
        cm.m[3][3] = logo_opacity;
        cm.m[4][4] = 1.0f;
        Gdiplus::ImageAttributes ia;
        ia.SetColorMatrix(&cm);

        Gdiplus::Rect dst(cx - dst_w / 2, cy - dst_h / 2, dst_w, dst_h);
        g.DrawImage(bmp, dst, 0, 0, (int)bmp->GetWidth(), (int)bmp->GetHeight(),
                    Gdiplus::UnitPixel, &ia);
    }   /* g flushes to the DC on scope exit, before the GDI text below */

    if (!show_label) return;

    EnsureFonts();
    if (!label_font_) return;

    const int v = (int)(255.0f * text_opacity);
    SetBkMode(dc, TRANSPARENT);

    HFONT old = (HFONT)SelectObject(dc, label_font_);
    SetTextColor(dc, RGB(v, v, v));
    const int logo_bottom = cy + dst_h / 2;
    const int max_y = (int)height - kLabelFontPx - 8;
    const int gap = std::clamp((int)((max_y - logo_bottom) * 0.35f), 8, 44);
    int label_y = logo_bottom + gap;
    if (label_y > max_y)              label_y = max_y;
    if (label_y < logo_bottom + 8)    label_y = logo_bottom + 8;  /* but below logo */
    RECT r{ 0, label_y, (int)width, label_y + kLabelFontPx * 2 };
    DrawTextW(dc, label.c_str(), (int)label.size(), &r,
              DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
}

void BootScreen::DrawAnimation(HDC dc, uint32_t width, uint32_t height) {
    switch (phase_) {
        case Phase::CerfFadeIn:
        case Phase::CerfHold:
            DrawLogoFrame(dc, width, height, cur_op_, false, L"", 0.0f);
            break;
        case Phase::TextFadeIn:
        case Phase::TextHold:
            DrawLogoFrame(dc, width, height, 1.0f, true, CurrentLabelText(), cur_op_);
            break;
        case Phase::Finished:
            break;
    }
}

void BootScreen::DrawHeldFinal(HDC dc, uint32_t width, uint32_t height) {
    DrawLogoFrame(dc, width, height, 1.0f, true, CurrentLabelText(), 1.0f);
}

void BootScreen::DrawHwStatusLine(HDC dc, uint32_t width, uint32_t height) {
    hw_line_rect_ = RECT{};
    const std::string last = emu_.Get<HwScreen>().LastLine();
    if (last.empty()) return;

    EnsureFonts();
    if (!disclaimer_font_) return;

    const float op = (phase_ == Phase::CerfFadeIn) ? cur_op_ : 1.0f;
    const int   dv = (int)(150.0f * op);

    const int bottom = (int)height - kBootBarPx - 4;
    RECT r{ kMargin, bottom - kHwLineHeightPx, (int)width - kMargin, bottom };

    const std::wstring wide = Utf8ToWide(last.c_str());
    HFONT old = (HFONT)SelectObject(dc, disclaimer_font_);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(dv, dv, dv));
    DrawTextW(dc, wide.c_str(), (int)wide.size(), &r,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, old);

    hw_line_rect_ = RECT{ 0, r.top - 2, (int)width, (int)height - kBootBarPx };
}

bool BootScreen::HitTestHwLine(int x, int y) const {
    return x >= hw_line_rect_.left && x < hw_line_rect_.right &&
           y >= hw_line_rect_.top  && y < hw_line_rect_.bottom;
}

void BootScreen::DrawWatermark(HDC dc, uint32_t width, uint32_t height) {
    DrawLogoFrame(dc, width, height, kDimOpacity, false, L"", 0.0f, true);
}

void BootScreen::Restart() {
    restart_req_.store(true, std::memory_order_release);
}
void BootScreen::OnFramebufferLatched() { fb_latched_req_.store(true); }
