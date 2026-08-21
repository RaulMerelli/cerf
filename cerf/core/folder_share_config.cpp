#include "folder_share_config.h"
#include "cerf_paths.h"
#include "device_config_refresh.h"

#define NOMINMAX
#include <windows.h>

REGISTER_SERVICE(FolderShareConfig);

void FolderShareConfig::OnReady() {
    ApplyFromConfig();
    emu_.Get<DeviceConfigRefresh>().RegisterListener(
        [this] { ApplyFromConfig(); });
}

void FolderShareConfig::ApplyFromConfig() {
    const std::string& p = emu_.Get<DeviceConfig>().share_folder;
    if (p.empty()) {
        Set(false, L"", L"");
        return;
    }

    const int n = MultiByteToWideChar(CP_ACP, 0, p.c_str(), -1, nullptr, 0);
    if (n <= 1) {
        Set(false, L"", L"");
        return;
    }
    std::wstring wp(n - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, p.c_str(), -1, &wp[0], n);

    if (!IsAbsoluteHostPath(p))
        wp = Utf8ToWide(GetCerfDir().c_str()) + wp;

    Set(true, std::move(wp), L"");
}
