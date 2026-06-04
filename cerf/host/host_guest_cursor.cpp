#define NOMINMAX

#include "host_guest_cursor.h"

#include "../core/cerf_emulator.h"
#include "../peripherals/cerf_virt/cerf_virt_cursor.h"

#include <vector>

REGISTER_SERVICE(HostGuestCursor);

namespace {

#ifndef CURSOR_CREATION_SCALING_NONE
#define CURSOR_CREATION_SCALING_NONE 1
#endif

/* Disable host cursor scaling for cursors created on this (UI) thread, so the
   guest cursor renders 1:1 — the guest framebuffer is 1:1, so its cursor must
   be too. Resolved dynamically: absent before Win10 1607, where it no-ops and
   the cursor falls back to the host's DPI/accessibility scaling. */
void DisableCursorScaling() {
    typedef UINT (WINAPI *PFN)(UINT);
    static PFN pfn = (PFN)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                         "SetThreadCursorCreationScaling");
    if (pfn) pfn(CURSOR_CREATION_SCALING_NONE);
}

/* Repack the guest 1bpp AND/XOR masks into native-size WORD-aligned MSB-first
   planes and build the HCURSOR at the guest's exact dimensions. Bit semantics
   match Win32 (verified WINCE600 SMI3DR cursor.cpp): AND=1 transparent,
   AND=0/XOR=0 black, AND=0/XOR=1 white — a direct copy. */
HCURSOR BuildCursor(const GuestCursorShape& s) {
    const int cx = (int)s.cx, cy = (int)s.cy, stride = (int)s.stride;
    if (cx <= 0 || cy <= 0) return nullptr;
    const int win_stride = ((cx + 15) / 16) * 2;

    std::vector<uint8_t> andP((size_t)win_stride * cy, 0xFF);  /* transparent */
    std::vector<uint8_t> xorP((size_t)win_stride * cy, 0x00);

    for (int row = 0; row < cy; ++row) {
        const uint8_t* and_row = s.bits.data() + (size_t)row * stride;
        const uint8_t* xor_row = s.bits.data() + (size_t)(cy + row) * stride;
        for (int col = 0; col < cx; ++col) {
            const uint8_t mask = (uint8_t)(0x80u >> (col & 7));
            const int byte = col >> 3;
            const int didx = row * win_stride + byte;
            if (!(and_row[byte] & mask)) andP[didx] &= (uint8_t)~mask;  /* opaque */
            if (xor_row[byte] & mask)    xorP[didx] |= mask;
        }
    }

    DisableCursorScaling();
    return CreateCursor(GetModuleHandleW(nullptr), (int)s.xhot, (int)s.yhot,
                        cx, cy, andP.data(), xorP.data());
}

}  /* namespace */

HostGuestCursor::~HostGuestCursor() {
    if (built_) DestroyCursor(built_);
}

HCURSOR HostGuestCursor::Resolve(bool& active) {
    auto* cur = emu_.TryGet<CerfVirtCursor>();
    if (!cur) { active = false; return nullptr; }

    const uint32_t seq = cur->Seq();
    if (seq != last_seq_) {
        last_seq_ = seq;
        GuestCursorShape s;
        if (cur->GetShape(s)) {
            active_ = true;
            HCURSOR old = built_;
            built_ = s.visible ? BuildCursor(s) : nullptr;
            if (old) DestroyCursor(old);
        }
    }
    active = active_;
    return built_;
}
