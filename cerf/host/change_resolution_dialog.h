#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>

/* Guest-additions "Change resolution" modal dialog. UI-thread only. */
class ChangeResolutionDialog : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    /* UI thread (widget menu action). Runs the modal dialog. */
    void Show();

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    void BuildControls(HWND hwnd);
    bool Apply(HWND hwnd);   /* true => accepted, close the dialog */

    /* A themed BUTTON draws its caption/label text via the immersive dark theme,
       which ignores WM_CTLCOLOR and yields black-on-dark; group frames and radio
       labels are therefore self-painted with explicit colors. */
    void PaintGroups(HDC dc);
    void PaintGroup(HDC dc, const RECT& frame, const wchar_t* caption, bool dark);
    void DrawRadio(const DRAWITEMSTRUCT* di);

    HWND hwnd_         = nullptr;
    bool done_         = false;
    int  reset_choice_ = 0;   /* 0 none / 1 soft / 2 hard */
};
