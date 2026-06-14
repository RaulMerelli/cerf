#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

enum class ShutdownChoice { Cancel, Exit, ExitSave };

/* Modal "shut down CERF?" prompt. UI thread only, run from a posted message
   (not inside WM_CLOSE). Returns the user's choice; HostWindow performs the
   save asynchronously — the dialog itself never saves or pumps for the save. */
class ShutdownDialog : public Service {
public:
    using Service::Service;
    void OnReady() override;

    /* UI thread. Runs the modal prompt and returns the choice. */
    ShutdownChoice Show();

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);
    void    Paint(HWND hwnd);
    void    StopTimer(HWND hwnd);

    HWND hwnd_        = nullptr;
    HWND chk_save_    = nullptr;
    HWND chk_remember_ = nullptr;
    bool decided_   = false;
    bool cancelled_ = false;
    bool save_      = false;
    bool timer_on_  = false;
    int  remaining_ = 0;
};
