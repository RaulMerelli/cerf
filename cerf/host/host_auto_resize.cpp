#define NOMINMAX
#include <windows.h>

#include "host_auto_resize.h"

#include "../core/cerf_emulator.h"
#include "../core/device_config.h"
#include "../peripherals/cerf_virt/cerf_virt_resize.h"
#include "host_widget_registry.h"

#include <string>
#include <vector>

namespace {
constexpr COLORREF kClrOn  = RGB(78, 201, 90);    /* green */
constexpr COLORREF kClrOff = RGB(140, 140, 140);  /* gray */
constexpr wchar_t  kGlyph  = L'\xE740';           /* Segoe MDL2 FullScreen */
}  /* namespace */

REGISTER_SERVICE(HostAutoResize);

bool HostAutoResize::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void HostAutoResize::OnReady() {
    glyph_font_ = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
    emu_.Get<HostWidgetRegistry>().Register(this);
}

HostAutoResize::~HostAutoResize() {
    if (glyph_font_) DeleteObject(glyph_font_);
}

void HostAutoResize::Toggle() {
    enabled_.store(!enabled_.load(std::memory_order_acquire),
                   std::memory_order_release);
}

void HostAutoResize::OnUserResizeEnd(uint32_t canvas_w, uint32_t canvas_h) {
    if (!Enabled() || canvas_w == 0 || canvas_h == 0) return;
    if (canvas_w == last_w_ && canvas_h == last_h_) return;
    last_w_ = canvas_w;
    last_h_ = canvas_h;
    emu_.Get<CerfVirtResize>().RequestResize(canvas_w, canvas_h, 32u);
}

std::wstring HostAutoResize::Tooltip() const {
    return Enabled()
        ? L"Auto-resize ON — guest follows the window size (click to disable)"
        : L"Auto-resize OFF — click to make the guest resolution follow the window";
}

void HostAutoResize::OnPrimaryAction() { Toggle(); }

std::vector<WidgetMenuItem> HostAutoResize::BuildMenu() {
    WidgetMenuItem it;
    it.label   = L"Resize guest to window";
    it.checked = Enabled();
    it.on_click = [this] { Toggle(); };
    return { std::move(it) };
}

void HostAutoResize::DrawIcon(HDC dc, const RECT& box) const {
    HGDIOBJ of = glyph_font_ ? SelectObject(dc, glyph_font_) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, Enabled() ? kClrOn : kClrOff);
    RECT r = box;
    DrawTextW(dc, &kGlyph, 1, &r, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    if (of) SelectObject(dc, of);
}

bool HostAutoResize::PollDirty() {
    const bool on = Enabled();
    if (on == last_drawn_on_) return false;
    last_drawn_on_ = on;
    return true;
}
