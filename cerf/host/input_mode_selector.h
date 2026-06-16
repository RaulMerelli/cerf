#pragma once

#define NOMINMAX
#include <windows.h>

#include "../core/service.h"
#include "host_widget.h"

#include <atomic>
#include <string>
#include <vector>

enum class InputMode { Pointer, Touch };

/* Touch mode is the fallback for boot flows that read the raw touch peripheral
   directly: the PocketPC calibrator polls raw ADC samples below the mouse
   abstraction, so the pointer path's mouse_event cannot drive it. */
class InputModeSelector : public Service, public HostWidget {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    InputMode Mode() const { return mode_.load(std::memory_order_relaxed); }
    void      SetMode(InputMode m) { mode_.store(m, std::memory_order_relaxed); }

    /* HostWidget */
    std::wstring WidgetName() const override { return L"Input device"; }
    WidgetGroup  Group() const override { return WidgetGroup::InputControl; }
    std::wstring Tooltip() const override;
    void OnPrimaryAction() override;
    std::vector<WidgetMenuItem> BuildMenu() override;
    void DrawIcon(HDC dc, const RECT& box) const override;
    bool PollDirty() override;
    void SaveState(StateWriter& w) const override;
    void RestoreState(StateReader& r) override;

private:
    std::atomic<InputMode> mode_{InputMode::Pointer};
    InputMode              drawn_mode_ = InputMode::Pointer;  /* UI-thread only */
};
