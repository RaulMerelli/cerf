#include "../core/service.h"
#include "../core/cerf_emulator.h"
#include "../core/device_config.h"
#include "host_window.h"

/* Applies the --full-screen launch preference: once the host window is up,
   enter the borderless-fullscreen mode (the Right Ctrl+F / View-menu toggle). */
namespace {
class HostStartupFullscreen : public Service {
public:
    using Service::Service;

    void OnReady() override {
        if (!emu_.Get<DeviceConfig>().start_fullscreen) return;
        /* Get<HostWindow> returns only after the window is shown (HostWindow::
           OnReady blocks on ui_ready_), so the HWND is live here. ToggleFullscreen
           manipulates the window, so it must run on the owning UI thread. */
        emu_.Get<HostWindow>().RunOnUiThread(
            [this] { emu_.Get<HostWindow>().ToggleFullscreen(); });
    }
};
}
REGISTER_SERVICE(HostStartupFullscreen);
