#include "pointer_widget.h"

#include "../core/cerf_emulator.h"
#include "../core/device_config.h"
#include "../state/state_stream.h"
#include "host_icon_cache.h"
#include "host_widget_registry.h"
#include "pointer_router.h"
#include "pointer_source.h"

REGISTER_SERVICE(PointerWidget);

bool PointerWidget::ShouldRegister() {
    if (emu_.Get<DeviceConfig>().guest_additions) return true;
    return emu_.Get<PointerRouter>().Sources().size() > 1;
}

void PointerWidget::OnReady() {
    emu_.Get<HostWidgetRegistry>().Register(this);
}

std::wstring PointerWidget::Tooltip() const {
    auto& router = emu_.Get<PointerRouter>();
    PointerSource* active = router.Active();
    std::wstring tip = active ? active->SourceName() : L"Pointing device";
    if (router.Sources().size() > 1) tip += L" — click to switch input device";
    return tip;
}

void PointerWidget::OnPrimaryAction() {
    emu_.Get<PointerRouter>().CycleNext();
}

std::vector<WidgetMenuItem> PointerWidget::BuildMenu() {
    std::vector<WidgetMenuItem> items;
    auto& router  = emu_.Get<PointerRouter>();
    auto  sources = router.Sources();
    if (sources.size() > 1) {
        PointerSource* active = router.Active();
        for (auto* s : sources) {
            WidgetMenuItem mi;
            mi.label    = s->SourceName();
            mi.checked  = (s == active);
            mi.on_click = [this, s] { emu_.Get<PointerRouter>().SetActive(s); };
            items.push_back(std::move(mi));
        }
    }
    return items;
}

void PointerWidget::DrawIcon(HDC dc, const RECT& box) const {
    const wchar_t* icon = L"ICON_INPUT_GA_POINTER";
    if (auto* a = emu_.Get<PointerRouter>().Active()) icon = a->IconResourceName();
    emu_.Get<HostIconCache>().DrawCentered(dc, box, icon);
}

bool PointerWidget::PollDirty() {
    const PointerSource* a = emu_.Get<PointerRouter>().Active();
    if (a == drawn_source_) return false;
    drawn_source_ = a;
    return true;
}

void PointerWidget::SaveState(StateWriter& w) const {
    PointerSource* a = emu_.Get<PointerRouter>().Active();
    const std::wstring name = a ? a->SourceName() : std::wstring();
    w.Write<uint32_t>(static_cast<uint32_t>(name.size()));
    w.WriteBytes(name.data(), name.size() * sizeof(wchar_t));
}

void PointerWidget::RestoreState(StateReader& r) {
    uint32_t n = 0;
    r.Read(n);
    if (n > 1024u) return;   /* corrupt; outer section frame realigns */
    std::wstring name(n, L'\0');
    r.ReadBytes(name.data(), n * sizeof(wchar_t));
    if (!name.empty()) emu_.Get<PointerRouter>().SetActiveByName(name);
}
