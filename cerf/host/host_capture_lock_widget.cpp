#define NOMINMAX
#include <windows.h>

#include "../core/cerf_emulator.h"
#include "../core/service.h"
#include "host_icon_cache.h"
#include "host_input_capture.h"
#include "host_widget.h"
#include "host_widget_registry.h"

#include <string>
#include <vector>

namespace {

/* The input-capture lock as a host-owned widget. WidgetGroup::InputCapture is
   the highest rank, so it stays rightmost in the bar (and menu-top) regardless
   of which other widgets are present. */
class HostCaptureLockWidget : public Service, public HostWidget {
public:
    using Service::Service;

    void OnReady() override {
        emu_.Get<HostWidgetRegistry>().Register(this);
    }

    std::wstring WidgetName() const override { return L"Input Capture"; }
    WidgetGroup  Group() const override { return WidgetGroup::InputCapture; }
    std::wstring Tooltip() const override {
        return emu_.Get<HostInputCapture>().IsCaptured()
            ? L"Input captured - Right Ctrl (or click) to release"
            : L"Input free - Right Ctrl (or click) to capture (Alt+Tab etc. -> guest)";
    }
    void OnPrimaryAction() override { emu_.Get<HostInputCapture>().Toggle(); }
    std::vector<WidgetMenuItem> BuildMenu() override {
        /* '\t' right-aligns the Right Ctrl hotkey hint, same as the native
           menu items. The hotkey itself is serviced by HostInputCapture's
           low-level keyboard hook, not a menu accelerator. */
        WidgetMenuItem it;
        it.label    = emu_.Get<HostInputCapture>().IsCaptured()
                          ? L"Release input capture\tRight Ctrl"
                          : L"Capture input\tRight Ctrl";
        it.on_click = [this] { emu_.Get<HostInputCapture>().Toggle(); };
        return { std::move(it) };
    }
    void DrawIcon(HDC dc, const RECT& box) const override {
        const bool cap = emu_.Get<HostInputCapture>().IsCaptured();
        emu_.Get<HostIconCache>().DrawCentered(
            dc, box, cap ? L"ICON_LOCK_ON" : L"ICON_LOCK_OFF");
    }
    bool PollDirty() override {
        const bool cap = emu_.Get<HostInputCapture>().IsCaptured();
        if (cap == last_drawn_cap_) return false;
        last_drawn_cap_ = cap;
        return true;
    }

private:
    bool last_drawn_cap_ = false;
};

}  /* namespace */

REGISTER_SERVICE(HostCaptureLockWidget);
