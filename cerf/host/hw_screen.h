#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

/* The host window's hardware screen: the text-mode RX/TX surface for guest UART
   / OEM debug output and CERF's own user-facing notices (power events,
   save/restore progress), plus the boot animation owned by HwBootAnimation. */
class HwScreen : public Service {
public:
    using Service::Service;
    ~HwScreen() override;

    /* Push one completed debug-console line. Trailing newline characters are
       stripped by the caller. Lines beyond kMaxLines drop the oldest. */
    void AddLine(std::string_view line);

    /* True once at least one line has been added. */
    bool HasOutput() const;

    /* Drop all buffered lines. HasOutput stays true; callers repopulate. */
    void Clear();

    /* Re-arm the text-mode gate: after this, the finished boot animation stays
       on the held logo (not text mode) until the NEXT AddLine. Called right
       after the reboot banner so a non-debug board doesn't snap from the OEM
       hold to a lone banner - it waits for genuine new guest TX first. */
    void ArmTextGate();

    /* Render into the host DIB. dc wraps the same pixels for GDI text/logo
       drawing; dib_bgra32 / width / height describe the raw surface. */
    void RenderInto(HDC dc, uint32_t* dib_bgra32, uint32_t width, uint32_t height);

private:
    static constexpr size_t kMaxLines = 500;

    mutable std::mutex      mtx_;
    std::deque<std::string> lines_;
    bool                    has_output_      = false;
    bool                    text_gate_armed_ = false;

    HFONT font_cache_[2] = { nullptr, nullptr };

    void DrawLog(HDC dc, uint32_t width, uint32_t height,
                 const std::vector<std::string>& lines);
    void DrawBootBar(uint32_t* dib_bgra32, uint32_t width, uint32_t height) const;
};
