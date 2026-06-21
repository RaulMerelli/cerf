#define NOMINMAX

#include "sec_flash.h"

#include "../core/cerf_emulator.h"
#include "../core/device_config.h"
#include "../core/log.h"
#include "../core/cerf_paths.h"
#include "../core/string_utils.h"

#include <windows.h>

#include <string>

REGISTER_SERVICE(SecFlash);

namespace {

std::string FindSecFile(const std::string& device_dir) {
    const std::wstring pattern = Utf8ToWide((device_dir + "*.sec").c_str());
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::string name;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            name = WideToUtf8(fd.cFileName);
            break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return name;
}

}  /* namespace */

bool SecFlash::ShouldRegister() {
    const auto& cfg = emu_.Get<DeviceConfig>();
    return !FindSecFile(GetDeviceDir(cfg.device_name)).empty();
}

void SecFlash::OnReady() {
    const auto&       cfg  = emu_.Get<DeviceConfig>();
    const std::string dir  = GetDeviceDir(cfg.device_name);
    const std::string name = FindSecFile(dir);
    if (name.empty()) return;

    if (!mf_.Open(dir + name)) {
        LOG(Caution, "SecFlash: failed to map %s%s\n", dir.c_str(), name.c_str());
        return;
    }
    if (!sec_.Open(mf_)) {
        LOG(Caution, "SecFlash: %s%s is not a valid .sec container\n",
            dir.c_str(), name.c_str());
        return;
    }
    LOG(Boot, "SecFlash: %s%s - %u chunks, %.1f MB de-chunked flash\n",
        dir.c_str(), name.c_str(), sec_.Header().chunk_count,
        double(FlashSize()) / 1024.0 / 1024.0);
}
