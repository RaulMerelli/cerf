#define NOMINMAX
#include <windows.h>

#include "host_auto_resize.h"

#include "../core/cerf_emulator.h"
#include "../core/device_config.h"
#include "../core/log.h"
#include "../peripherals/cerf_virt/cerf_virt_resize.h"
#include "host_widget_registry.h"
#include "task_manager_window.h"

#include <string>
#include <vector>

namespace {
constexpr COLORREF kClrOn  = RGB(78, 201, 90);    /* green */
constexpr COLORREF kClrOff = RGB(140, 140, 140);  /* gray */
}  /* namespace */

REGISTER_SERVICE(HostAutoResize);

bool HostAutoResize::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void HostAutoResize::OnReady() {
    /* cerf.rc resource 1: the application icon. */
    icon_ = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1),
                              IMAGE_ICON, 16, 16, 0);
    if (!icon_) {
        LOG(Caution, "HostAutoResize: LoadImageW(icon 1) failed (gle=%lu)\n",
            GetLastError());
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    emu_.Get<HostWidgetRegistry>().Register(this);
}

HostAutoResize::~HostAutoResize() = default;

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
        ? L"Guest Additions — auto-resize ON (click to disable, right-click for tools)"
        : L"Guest Additions — auto-resize OFF (click to enable, right-click for tools)";
}

void HostAutoResize::OnPrimaryAction() { Toggle(); }

std::vector<WidgetMenuItem> HostAutoResize::BuildMenu() {
    std::vector<WidgetMenuItem> items;

    WidgetMenuItem header;
    header.label   = L"CERF Guest Additions";
    header.enabled = false;
    items.push_back(std::move(header));

    items.push_back(WidgetMenuItem{});   /* separator */

    WidgetMenuItem taskmgr;
    taskmgr.label    = L"Task manager…";
    taskmgr.on_click = [this] { emu_.Get<TaskManagerWindow>().Show(); };
    items.push_back(std::move(taskmgr));

    WidgetMenuItem resize;
    resize.label    = L"Resize guest to window";
    resize.checked  = Enabled();
    resize.on_click = [this] { Toggle(); };
    items.push_back(std::move(resize));

    return items;
}

void HostAutoResize::DrawIcon(HDC dc, const RECT& box) const {
    const int cx = (box.left + box.right) / 2;
    const int cy = (box.top + box.bottom) / 2;
    constexpr int e = 7;  /* half-extent of the bracket square */
    constexpr int a = 5;  /* bracket arm length */
    constexpr int t = 2;  /* bracket arm thickness */
    const RECT arms[8] = {
        { cx - e,     cy - e,     cx - e + a, cy - e + t },  /* top-left */
        { cx - e,     cy - e,     cx - e + t, cy - e + a },
        { cx + e - a, cy - e,     cx + e,     cy - e + t },  /* top-right */
        { cx + e - t, cy - e,     cx + e,     cy - e + a },
        { cx - e,     cy + e - t, cx - e + a, cy + e     },  /* bottom-left */
        { cx - e,     cy + e - a, cx - e + t, cy + e     },
        { cx + e - a, cy + e - t, cx + e,     cy + e     },  /* bottom-right */
        { cx + e - t, cy + e - a, cx + e,     cy + e     },
    };
    HBRUSH br = CreateSolidBrush(Enabled() ? kClrOn : kClrOff);
    for (const RECT& r : arms) FillRect(dc, &r, br);
    DeleteObject(br);

    DrawIconEx(dc, cx - 5, cy - 5, icon_, 10, 10, 0, nullptr, DI_NORMAL);
}

bool HostAutoResize::PollDirty() {
    const bool on = Enabled();
    if (on == last_drawn_on_) return false;
    last_drawn_on_ = on;
    return true;
}
