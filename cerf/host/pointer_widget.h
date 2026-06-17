#pragma once

#define NOMINMAX
#include <windows.h>

#include "../core/service.h"
#include "host_widget.h"

#include <string>
#include <vector>

class PointerSource;

/* Status-bar widget to pick the active pointing device (mirror of
   KeyboardWidget). Left-click cycles sources; right-click lists them as radios;
   the icon and tooltip reflect the active source. */
class PointerWidget : public Service, public HostWidget {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    std::wstring WidgetName() const override { return L"Pointing device"; }
    WidgetGroup  Group() const override { return WidgetGroup::InputControl; }
    std::wstring Tooltip() const override;
    void OnPrimaryAction() override;
    std::vector<WidgetMenuItem> BuildMenu() override;
    void DrawIcon(HDC dc, const RECT& box) const override;
    bool PollDirty() override;
    void SaveState(StateWriter& w) const override;
    void RestoreState(StateReader& r) override;

private:
    const PointerSource* drawn_source_ = nullptr;  /* UI-thread only */
};
