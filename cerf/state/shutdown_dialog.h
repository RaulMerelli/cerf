#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

enum class ShutdownChoice { Cancel, Exit, ExitSave };

/* Why the prompt is showing. Drives the auto-decide countdown (window-close
   only), the body text, and the Cancel/Resume button label. */
enum class ShutdownTrigger { WindowClose, DeepSleep };

/* Modal "shut down CERF?" prompt. UI thread only, run from a posted message
   (not inside WM_CLOSE). Returns the user's choice; HostWindow performs the
   save asynchronously - the dialog itself never saves or pumps for the save. */
class ShutdownDialog : public Service {
public:
    using Service::Service;
    void OnReady() override;

    /* UI thread. Runs the modal prompt and returns the choice. WindowClose runs
       an auto-decide countdown and "shut down" wording. DeepSleep has NO timer
       (the user may be away and an auto-decide could exit + discard guest state
       unattended), deep-sleep wording, and a "Resume" button in place of Cancel. */
    ShutdownChoice Show(ShutdownTrigger trigger = ShutdownTrigger::WindowClose);

    void DismissAsCancel();

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);
    void    Paint(HWND hwnd);
    void    StopTimer(HWND hwnd);
    int     BarFillWidth() const;
    int     RemainingSeconds() const;

    HWND hwnd_        = nullptr;
    HWND chk_save_    = nullptr;
    HWND chk_remember_ = nullptr;
    bool decided_   = false;
    bool cancelled_ = false;
    bool save_      = false;
    bool timer_on_  = false;
    unsigned long long deadline_tick_ = 0;
    int  remaining_ms_ = 0;
    int  painted_fill_w_ = -1;
    int  painted_secs_   = -1;
    ShutdownTrigger trigger_ = ShutdownTrigger::WindowClose;
};
