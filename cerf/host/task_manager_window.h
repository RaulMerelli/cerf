#pragma once

#include "../core/service.h"
#include "../peripherals/cerf_virt/cerf_virt_task_manager.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

/* Non-modal guest task-manager window (guest additions): live process table
   with switch-to / kill / run controls, backed by CerfVirtTaskManager.
   UI-thread only. */
class TaskManagerWindow : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    /* UI thread (widget menu action). Creates the window or raises it. */
    void Show();

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK RunEditProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    void BuildControls();
    void LayoutControls();
    void OnTick();
    void Repopulate();
    void DoSwitchTo();
    void DoKill();
    void DoRun();
    void ShowRowMenu(int item, POINT screen_pt);
    bool SelectedPid(uint32_t* pid) const;
    void SetStatus(const std::wstring& text, COLORREF clr);

    HWND hwnd_       = nullptr;
    HWND list_       = nullptr;
    HWND run_label_  = nullptr;
    HWND run_edit_   = nullptr;
    HWND btn_run_    = nullptr;
    HWND btn_switch_ = nullptr;
    HWND btn_kill_   = nullptr;
    HWND status_     = nullptr;
    WNDPROC run_edit_base_proc_ = nullptr;

    uint64_t shown_gen_  = 0;
    uint32_t run_ticket_ = 0;
    COLORREF status_clr_ = RGB(0, 0, 0);
    std::vector<CerfVirtTaskManager::ProcEntry> shown_procs_;
};
