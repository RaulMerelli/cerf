#include "initial_window_size.h"

#include "../boards/board_context.h"
#include "../core/cerf_emulator.h"
#include "../core/device_config.h"

REGISTER_SERVICE(InitialWindowSize);

InitialWindowSize::Size InitialWindowSize::Resolve() const {
    const auto& dc = emu_.Get<DeviceConfig>();
    uint32_t w = dc.board_configurable_screen_width;
    uint32_t h = dc.board_configurable_screen_height;

    if (!dc.guest_additions && !dc.board_configurable_screen_explicit)
        if (auto pref = emu_.Get<BoardContext>().GetPreferredWindowSize()) {
            w = pref->width;
            h = pref->height;
        }

    return { w, h };
}
