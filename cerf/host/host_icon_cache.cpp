#define NOMINMAX
#include "host_icon_cache.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"

#include <commctrl.h>   /* LoadIconWithScaleDown */

REGISTER_SERVICE(HostIconCache);

HostIconCache::~HostIconCache() {
    for (auto& e : cache_) DestroyIcon(e.second);
}

void HostIconCache::DrawCentered(HDC dc, const RECT& box,
                                 const wchar_t* res_name) {
    const int bw = box.right - box.left;
    const int bh = box.bottom - box.top;
    int px = (bw < bh ? bw : bh) - 4;   /* small padding inside the slot box */

    HICON h = Resolve(res_name, px);
    if (!h) {
        LOG(Caution, "HostIconCache: ICON resource '%ls' absent from cerf.exe "
            "(missing/typo'd cerf.rc entry)\n", res_name);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    DrawIconEx(dc, box.left + (bw - px) / 2, box.top + (bh - px) / 2,
               h, px, px, 0, nullptr, DI_NORMAL);
}

HICON HostIconCache::Resolve(const wchar_t* res_name, int px) {
    std::pair<std::wstring, int> key(res_name, px);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;

    HICON h = nullptr;
    LoadIconWithScaleDown(GetModuleHandleW(nullptr), res_name, px, px, &h);
    if (h) cache_[key] = h;
    return h;
}
