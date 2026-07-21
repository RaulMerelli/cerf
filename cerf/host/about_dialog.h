#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

namespace Gdiplus { class Bitmap; }

class AboutDialog : public Service {
public:
    using Service::Service;
    ~AboutDialog() override;

    void OnReady() override;

    /* UI thread (menu action). Runs the modal dialog. */
    void Show();

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    static BOOL CALLBACK SetChildFontProc(HWND, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    void BuildControls(HWND hwnd);
    void ApplyCustomFonts();      /* after HostDarkMode stomps the UI font */
    void PaintBand(HDC dc, int origin_x, int origin_y);
    bool OpenLink(LPARAM notify); /* SysLink NM_CLICK/NM_RETURN -> ShellExecute */

    int S(int v) const;

    HWND hwnd_  = nullptr;
    HWND title_ = nullptr;
    bool done_  = false;

    UINT dpi_ = USER_DEFAULT_SCREEN_DPI;

    Gdiplus::Bitmap* band_ = nullptr;
    int  band_px_w_ = 0;
    int  band_px_h_ = 0;

    HFONT title_font_ = nullptr;
    HFONT ui_font_    = nullptr;
};
