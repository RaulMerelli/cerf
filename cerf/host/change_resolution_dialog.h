#pragma once

#include "modal_dialog.h"

#include <cstdint>

/* Guest-additions "Change resolution" modal dialog. UI-thread only. */
class ChangeResolutionDialog : public ModalDialog {
public:
    using ModalDialog::ModalDialog;

    bool ShouldRegister() override;

    bool Show(HWND owner, bool force_soft_reset);

private:
    void BuildControls(HWND hwnd) override;
    void OnPaint(HDC dc) override;
    void OnCommand(int id, int notify) override;
    bool OnDrawItem(const DRAWITEMSTRUCT* di) override;

    bool Apply(HWND hwnd);   /* true => accepted, close the dialog */

    /* A themed BUTTON draws its caption/label text via the immersive dark theme,
       which ignores WM_CTLCOLOR and yields black-on-dark; group frames and radio
       labels are therefore self-painted with explicit colors. */
    void PaintGroups(HDC dc);
    void PaintGroup(HDC dc, const RECT& frame, const wchar_t* caption, bool dark,
                    bool enabled);
    void DrawRadio(const DRAWITEMSTRUCT* di);

    bool accepted_     = false;
    bool reset_locked_ = false;
    int  reset_choice_ = 0;   /* 0 none / 1 soft / 2 hard */
};
