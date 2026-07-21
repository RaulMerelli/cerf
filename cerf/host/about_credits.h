#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

#include <string>
#include <vector>

class AboutCredits : public Service {
public:
    using Service::Service;

    void OnReady() override;

    HWND Create(HWND parent, HFONT font, int x, int y, int w, int h, UINT dpi);

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    struct Line {
        HWND hwnd;
        int  y;
        bool has_link;
    };

    bool LineHasLink(HWND h) const;

    void BuildLines(int text_w);
    void AddLine(const std::wstring& markup, int text_w, int& y);
    void UpdateScrollInfo();
    void ScrollTo(int pos);

    int S(int v) const;

    HWND  pane_ = nullptr;
    HFONT font_ = nullptr;
    UINT  dpi_  = USER_DEFAULT_SCREEN_DPI;

    std::vector<Line> lines_;
    int content_h_  = 0;
    int scroll_pos_ = 0;
};
