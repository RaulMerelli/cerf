#pragma once
#include <cstdint>

struct CerfConfig {
    const char* device_override = nullptr;

    bool disable_network = false;

    const char* log_file = nullptr;
    bool flush_outputs = false;
    uint64_t no_log_mask = 0;

    uint32_t screen_width  = 0;
    uint32_t screen_height = 0;

    uint32_t start_window_width  = 800;
    uint32_t start_window_height = 600;

    bool poc_rom_injection = false;
};

bool ParseCerfArgs(int argc, char* argv[], CerfConfig& cfg);
