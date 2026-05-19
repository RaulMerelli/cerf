#include "main_config.h"
#include "log.h"
#include "cli_helpers.h"
#include <cstring>
#include <cstdlib>

bool ParseCerfArgs(int argc, char* argv[], CerfConfig& cfg) {
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--log=", 6) == 0) {
            Log::SetEnabled(Log::ParseCategories(argv[i] + 6));
        } else if (strncmp(argv[i], "--no-log=", 9) == 0) {
            cfg.no_log_mask |= Log::ParseCategories(argv[i] + 9);
        } else if (strncmp(argv[i], "--log-file=", 11) == 0) {
            cfg.log_file = argv[i] + 11;
        } else if (strcmp(argv[i], "--flush-outputs") == 0) {
            cfg.flush_outputs = true;
        } else if (strncmp(argv[i], "--device=", 9) == 0) {
            cfg.device_override = argv[i] + 9;
        } else if (strcmp(argv[i], "--allow-flood") == 0) {
            Log::SetAllowFlood(true);
        } else if (strcmp(argv[i], "--disable-network") == 0) {
            cfg.disable_network = true;
        } else if (strncmp(argv[i], "--screen-width=", 15) == 0) {
            int n = atoi(argv[i] + 15);
            if (n < 1) {
                LOG(Caution, "Invalid --screen-width: %s\n", argv[i] + 15);
                return false;
            }
            cfg.screen_width = (uint32_t)n;
        } else if (strncmp(argv[i], "--screen-height=", 16) == 0) {
            int n = atoi(argv[i] + 16);
            if (n < 1) {
                LOG(Caution, "Invalid --screen-height: %s\n", argv[i] + 16);
                return false;
            }
            cfg.screen_height = (uint32_t)n;
        } else if (strncmp(argv[i], "--start-window-width=", 21) == 0) {
            int n = atoi(argv[i] + 21);
            if (n < 1) {
                LOG(Caution, "Invalid --start-window-width: %s\n", argv[i] + 21);
                return false;
            }
            cfg.start_window_width = (uint32_t)n;
        } else if (strncmp(argv[i], "--start-window-height=", 22) == 0) {
            int n = atoi(argv[i] + 22);
            if (n < 1) {
                LOG(Caution, "Invalid --start-window-height: %s\n", argv[i] + 22);
                return false;
            }
            cfg.start_window_height = (uint32_t)n;
        } else if (strcmp(argv[i], "--poc-rom-injection") == 0) {
            cfg.poc_rom_injection = true;
        } else if (strcmp(argv[i], "--quiet") == 0) {
            Log::SetEnabled(Log::MASK_NONE);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            PrintUsage(argv[0]);
            return false;
        } else {
            LOG(Caution, "Unknown argument: %s (use --help)\n", argv[i]);
            return false;
        }
    }

    if (cfg.no_log_mask) {
        Log::SetEnabled(Log::GetEnabled() & ~cfg.no_log_mask);
    }

    if (cfg.flush_outputs) {
        Log::SetFlush(true);
    }

    if (cfg.log_file) {
        Log::SetFile(cfg.log_file);
    }

    return true;
}
