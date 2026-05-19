#define NOMINMAX

#include "uart_screen.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"

#include <algorithm>
#include <cstring>

REGISTER_SERVICE(UartScreen);

namespace {

/* Logo sizing — fraction of min(width, height), clamped. The splash
   state uses a bigger logo (focal); the corner state uses a smaller
   one (decorative, out of the boot log's way). */
constexpr uint32_t kLogoSplashFraction = 4;   /* 1/4 of min dim */
constexpr uint32_t kLogoCornerFraction = 8;   /* 1/8 of min dim */
constexpr uint32_t kLogoMinPx          = 48;
constexpr uint32_t kLogoMaxPx          = 256;

/* Boot-log layout. */
constexpr int      kEdgeMarginPx    = 1;
constexpr COLORREF kLogTextColor    = RGB(255, 255, 255);
constexpr COLORREF kBgTransparent   = 0;  /* unused — TRANSPARENT bk mode */

constexpr int      kFontHeightSmall          = 14;
constexpr int      kFontHeightRegular        = 16;
constexpr uint32_t kSmallTierThresholdPx     = 480;

}  /* namespace */

UartScreen::~UartScreen() {
    for (HFONT& f : font_cache_) {
        if (f) {
            DeleteObject(f);
            f = nullptr;
        }
    }
}

void UartScreen::AddLine(std::string_view line) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (lines_.size() >= kMaxLines) lines_.pop_front();
    lines_.emplace_back(line);
    has_output_ = true;
}

bool UartScreen::HasOutput() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return has_output_;
}

void UartScreen::RenderInto(HDC dc,
                            uint32_t* dib_bgra32,
                            uint32_t  width,
                            uint32_t  height) {
    /* Black-fill via raw pixel write — faster than PatBlt with a
       black brush and avoids a brush-creation round-trip. */
    std::memset(dib_bgra32, 0, (size_t)width * height * 4u);

    /* Snapshot the lines + state under the lock; do the drawing
       outside the lock so we don't block AddLine for the duration
       of GDI calls. */
    bool has_output;
    std::vector<std::string> snapshot;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        has_output = has_output_;
        snapshot.assign(lines_.begin(), lines_.end());
    }

    /* Geometry shared by both states. */
    const uint32_t min_dim = (width < height) ? width : height;

    if (!has_output) {
        /* State A: logo at center, no log. */
        uint32_t icon_sz = min_dim / kLogoSplashFraction;
        icon_sz = std::clamp(icon_sz, kLogoMinPx, kLogoMaxPx);
        HICON icon = (HICON)LoadImageW(GetModuleHandleW(nullptr),
                                       MAKEINTRESOURCEW(1),
                                       IMAGE_ICON,
                                       (int)icon_sz, (int)icon_sz,
                                       LR_DEFAULTCOLOR);
        if (icon) {
            const int x = ((int)width  - (int)icon_sz) / 2;
            const int y = ((int)height - (int)icon_sz) / 2;
            DrawIconEx(dc, x, y, icon, (int)icon_sz, (int)icon_sz,
                       0, nullptr, DI_NORMAL);
            DestroyIcon(icon);
        }
        return;
    }

    /* State B: logo at bottom-right, log scrolling at top-left. */
    uint32_t icon_sz = min_dim / kLogoCornerFraction;
    icon_sz = std::clamp(icon_sz, kLogoMinPx, kLogoMaxPx);
    HICON icon = (HICON)LoadImageW(GetModuleHandleW(nullptr),
                                   MAKEINTRESOURCEW(1),
                                   IMAGE_ICON,
                                   (int)icon_sz, (int)icon_sz,
                                   LR_DEFAULTCOLOR);
    if (icon) {
        const int x = (int)width  - (int)icon_sz - kEdgeMarginPx;
        const int y = (int)height - (int)icon_sz - kEdgeMarginPx;
        DrawIconEx(dc, x, y, icon, (int)icon_sz, (int)icon_sz,
                   0, nullptr, DI_NORMAL);
        DestroyIcon(icon);
    }

    /* Pick the font tier for this window size. Lazy-create on
       first use; cached across ticks. */
    const bool small_tier = (width  < kSmallTierThresholdPx) ||
                            (height < kSmallTierThresholdPx);
    const int  idx    = small_tier ? 0 : 1;
    const int  height_px = small_tier ? kFontHeightSmall
                                      : kFontHeightRegular;
    if (!font_cache_[idx]) {
        font_cache_[idx] = CreateFontW(
            -height_px,            /* negative = character height in px */
            0,                     /* width auto */
            0, 0,                  /* escapement, orientation */
            FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            NONANTIALIASED_QUALITY,    /* crisp pixel look */
            FIXED_PITCH | FF_MODERN,
            L"Fixedsys");
    }
    HFONT font = font_cache_[idx];
    if (!font) return;  /* CreateFontW failed; skip text */

    HFONT old_font = (HFONT)SelectObject(dc, font);
    SetTextColor(dc, kLogTextColor);
    SetBkMode(dc, TRANSPARENT);

    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    const int line_height = (int)tm.tmHeight;
    /* Fixedsys is FIXED_PITCH, so tmAveCharWidth is the actual cell
       width — same number TextOutA advances by per byte. */
    const int char_width  = (int)tm.tmAveCharWidth;
    if (line_height <= 0 || char_width <= 0) {
        SelectObject(dc, old_font);
        return;
    }

    const int avail_h    = (int)height - 2 * kEdgeMarginPx;
    const int avail_w    = (int)width  - 2 * kEdgeMarginPx;
    const int max_lines  = (avail_h > 0) ? avail_h / line_height : 0;
    const int chars_wide = (avail_w > 0) ? avail_w / char_width  : 0;
    if (max_lines <= 0 || chars_wide <= 0) {
        SelectObject(dc, old_font);
        return;
    }

    std::vector<std::string_view> wrapped;
    wrapped.reserve(snapshot.size());
    for (const std::string& line : snapshot) {
        if (line.empty()) {
            wrapped.emplace_back();
            continue;
        }
        size_t pos = 0;
        while (pos < line.size()) {
            const size_t take = std::min((size_t)chars_wide,
                                         line.size() - pos);
            wrapped.emplace_back(line.data() + pos, take);
            pos += take;
        }
    }

    /* Show the most-recent max_lines wrapped chunks; oldest visible
       chunk sits at the top, newest at the bottom-just-above-logo. */
    const int total = (int)wrapped.size();
    const int start = (total > max_lines) ? (total - max_lines) : 0;
    int y = kEdgeMarginPx;
    for (int i = start; i < total; ++i) {
        const std::string_view& chunk = wrapped[i];
        TextOutA(dc, kEdgeMarginPx, y,
                 chunk.data(), (int)chunk.size());
        y += line_height;
    }

    SelectObject(dc, old_font);
}
