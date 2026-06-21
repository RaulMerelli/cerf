#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>

namespace Gdiplus { class Bitmap; }

/* The "About CERF" modal dialog: logo, version, current device, real clickable
   GitHub/Discord links (SysLink), credits - dark-theme aware. UI-thread only.
   Hidden: the Konami code replays the boot-logo spin inside the box. */
class AboutDialog : public Service {
public:
    using Service::Service;
    ~AboutDialog() override;

    void OnReady() override;

    /* UI thread (menu action). Runs the modal dialog. */
    void Show();

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    void BuildControls(HWND hwnd);
    void ApplyCustomFonts();      /* after HostDarkMode stomps the UI font */
    void PaintLogo(HDC dc, int origin_x, int origin_y);
    bool OpenLink(LPARAM notify); /* SysLink NM_CLICK/NM_RETURN -> ShellExecute */

    /* Konami easter egg: ↑↑↓↓←→←→ B A -> spin the logo. */
    void TrackKonami(int vk);
    void StartEgg();
    void AdvanceEgg();

    HWND hwnd_      = nullptr;
    HWND title_     = nullptr;
    bool done_      = false;

    Gdiplus::Bitmap* logo_ = nullptr;
    HFONT title_font_ = nullptr;

    int       konami_idx_ = 0;
    bool      egg_active_  = false;
    uint64_t  egg_start_   = 0;
    float     egg_angle_   = 0.0f;
    float     egg_scale_   = 1.0f;
};
