#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

class ModalDialog : public Service {
public:
    using Service::Service;

protected:
    void RunModal(HWND owner, const wchar_t* class_name, const wchar_t* title,
                  int client_w, int client_h);

    void Finish() { done_ = true; }
    void PostDismiss();

    HWND Hwnd() const { return hwnd_; }
    bool IsOpen() const { return hwnd_ != nullptr; }

    virtual void BuildControls(HWND hwnd)                = 0;
    virtual void OnPaint(HDC)                            {}
    virtual void OnCommand(int, int)                     {}
    virtual bool OnDrawItem(const DRAWITEMSTRUCT*)       { return false; }

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    HWND hwnd_ = nullptr;
    bool done_ = false;
};
