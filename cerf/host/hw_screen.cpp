#define NOMINMAX

#include "hw_screen.h"

#include "../core/cerf_emulator.h"
#include "emulation_pause.h"
#include "hw_boot_animation.h"
#include "uart_boot_bar_data.h"

#include <algorithm>
#include <cstring>

REGISTER_SERVICE(HwScreen);

namespace {

constexpr int      kEdgeMarginPx = 1;
constexpr COLORREF kLogTextColor   = RGB(205, 205, 205);
constexpr COLORREF kErrorTextColor = RGB(255, 110, 110);  /* light-red */
constexpr COLORREF kWarnTextColor  = RGB(235, 210,  90);  /* amber */

/* Case-insensitive substring search; needle must already be lower-case. */
bool ContainsCI(std::string_view hay, std::string_view needle) {
    if (needle.empty() || needle.size() > hay.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        size_t j = 0;
        for (; j < needle.size(); ++j) {
            char a = hay[i + j];
            if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
            if (a != needle[j]) break;
        }
        if (j == needle.size()) return true;
    }
    return false;
}

/* Severity tint for one debug-console line. Error keywords win over warning so a
   line mentioning both reads as the more urgent colour. "fail" stems catch
   failed/failure/fails. */
COLORREF ClassifyLineColor(std::string_view line) {
    if (ContainsCI(line, "abort") || ContainsCI(line, "fail") ||
        ContainsCI(line, "error") || ContainsCI(line, "exception"))
        return kErrorTextColor;
    if (ContainsCI(line, "warning")) return kWarnTextColor;
    return kLogTextColor;
}

constexpr int      kFontHeightSmall      = 14;
constexpr int      kFontHeightRegular    = 16;
constexpr uint32_t kSmallTierThresholdPx = 480;

}  /* namespace */

HwScreen::~HwScreen() {
    for (HFONT& f : font_cache_) {
        if (f) { DeleteObject(f); f = nullptr; }
    }
}

void HwScreen::AddLine(std::string_view line) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (lines_.size() >= kMaxLines) lines_.pop_front();
    lines_.emplace_back(line);
    has_output_      = true;
    text_gate_armed_ = false;   /* a new line disarms the post-reboot hold gate */
}

bool HwScreen::HasOutput() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return has_output_;
}

void HwScreen::Clear() {
    std::lock_guard<std::mutex> lk(mtx_);
    lines_.clear();
}

void HwScreen::ArmTextGate() {
    std::lock_guard<std::mutex> lk(mtx_);
    text_gate_armed_ = true;
}

void HwScreen::RenderInto(HDC dc, uint32_t* dib_bgra32,
                          uint32_t width, uint32_t height) {
    std::memset(dib_bgra32, 0, (size_t)width * height * 4u);

    auto& anim = emu_.Get<HwBootAnimation>();
    anim.Advance(emu_.Get<EmulationPause>().AnimationTickMs());

    bool has_output, gate_armed;
    std::vector<std::string> snapshot;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        has_output = has_output_;
        gate_armed = text_gate_armed_;
        snapshot.assign(lines_.begin(), lines_.end());
    }

    /* The boot animation owns the screen until it finishes, even once output
       has been buffered - the buffered text then appears the instant it ends. */
    if (!anim.Finished()) {
        anim.DrawInto(dc, width, height);
        DrawBootBar(dib_bgra32, width, height);
        return;
    }

    /* An armed gate keeps the held logo even with output already buffered; it
       clears on the next AddLine, so the boot logo persists until fresh TX.
       With the animation disabled there is no hold to honour - show text. */
    if (has_output && (!gate_armed || !anim.Enabled())) {
        anim.DrawDimmedCenterLogo(dc, width, height);
        DrawLog(dc, width, height, snapshot);
    } else {
        anim.DrawHeldFinal(dc, width, height);
    }
    DrawBootBar(dib_bgra32, width, height);
}

void HwScreen::DrawLog(HDC dc, uint32_t width, uint32_t height,
                       const std::vector<std::string>& snapshot) {
    const bool small_tier = (width < kSmallTierThresholdPx) ||
                            (height < kSmallTierThresholdPx);
    const int idx       = small_tier ? 0 : 1;
    const int height_px = small_tier ? kFontHeightSmall : kFontHeightRegular;
    if (!font_cache_[idx]) {
        font_cache_[idx] = CreateFontW(
            -height_px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Fixedsys");
    }
    HFONT font = font_cache_[idx];
    if (!font) return;

    HFONT old_font = (HFONT)SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);

    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    const int line_height = (int)tm.tmHeight;
    const int char_width  = (int)tm.tmAveCharWidth;
    if (line_height <= 0 || char_width <= 0) { SelectObject(dc, old_font); return; }

    const int avail_h    = (int)height - 2 * kEdgeMarginPx;
    const int avail_w    = (int)width  - 2 * kEdgeMarginPx;
    const int max_lines  = (avail_h > 0) ? avail_h / line_height : 0;
    const int chars_wide = (avail_w > 0) ? avail_w / char_width  : 0;
    if (max_lines <= 0 || chars_wide <= 0) { SelectObject(dc, old_font); return; }

    struct Chunk { std::string_view text; COLORREF color; };
    std::vector<Chunk> wrapped;
    wrapped.reserve(snapshot.size());
    for (const std::string& line : snapshot) {
        const COLORREF color = ClassifyLineColor(line);
        if (line.empty()) { wrapped.push_back({ {}, color }); continue; }
        size_t pos = 0;
        while (pos < line.size()) {
            const size_t take = std::min((size_t)chars_wide, line.size() - pos);
            wrapped.push_back({ { line.data() + pos, take }, color });
            pos += take;
        }
    }

    const int total = (int)wrapped.size();
    const int start = (total > max_lines) ? (total - max_lines) : 0;
    int y = kEdgeMarginPx;
    for (int i = start; i < total; ++i) {
        const Chunk& chunk = wrapped[i];
        SetTextColor(dc, chunk.color);
        TextOutA(dc, kEdgeMarginPx, y, chunk.text.data(), (int)chunk.text.size());
        y += line_height;
    }
    SelectObject(dc, old_font);
}

void HwScreen::DrawBootBar(uint32_t* dib_bgra32,
                           uint32_t width, uint32_t height) const {
    if (height < kUartBootBarHeight || width == 0) return;

    const uint64_t cycle_ms = 4000;
    const uint32_t phase_ms =
        (uint32_t)(emu_.Get<EmulationPause>().AnimationTickMs() % cycle_ms);
    const int      x_origin = (int)(((uint64_t)phase_ms * width) / cycle_ms);
    const int      y_origin = (int)height - (int)kUartBootBarHeight;

    for (uint32_t py = 0; py < kUartBootBarHeight; ++py) {
        const int dst_y = y_origin + (int)py;
        if (dst_y < 0 || dst_y >= (int)height) continue;
        const uint32_t* src_row = &kUartBootBarPixels[py * kUartBootBarWidth];
        uint32_t*       dst_row = dib_bgra32 + (size_t)dst_y * width;
        for (uint32_t px = 0; px < kUartBootBarWidth; ++px) {
            const int dst_x = (x_origin + (int)px) % (int)width;
            const uint32_t s  = src_row[px];
            const uint32_t sa = (s >> 24) & 0xFFu;
            if (sa == 0) continue;
            const uint32_t sr = (s >> 16) & 0xFFu;
            const uint32_t sg = (s >>  8) & 0xFFu;
            const uint32_t sb =  s        & 0xFFu;
            const uint32_t d  = dst_row[dst_x];
            const uint32_t dr = (d >> 16) & 0xFFu;
            const uint32_t dg = (d >>  8) & 0xFFu;
            const uint32_t db =  d        & 0xFFu;
            const uint32_t inv = 255u - sa;
            const uint32_t out_r = (sr * sa + dr * inv) / 255u;
            const uint32_t out_g = (sg * sa + dg * inv) / 255u;
            const uint32_t out_b = (sb * sa + db * inv) / 255u;
            dst_row[dst_x] = 0xFF000000u | (out_r << 16) | (out_g << 8) | out_b;
        }
    }
}
