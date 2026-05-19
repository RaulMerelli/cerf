#include "cli_helpers.h"
#include "log.h"
#include <cstdio>

void PrintUsage(const char* prog) {
    printf("CERF — Windows CE virtual platform\n");
    printf("Boots unmodified Windows CE / Mobile / Phone ROMs on x64 Windows.\n\n");
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  --device=NAME            Bundle to boot (default from cerf.json)\n");
    printf("  --log=CATEGORIES         Enable only listed log categories (comma-sep)\n");
    printf("  --no-log=CATEGORIES      Disable specific categories\n");
    printf("  --log-file=PATH          Write logs to PATH (default cerf.log next to exe)\n");
    printf("  --flush-outputs          Flush log file after every write\n");
    printf("  --allow-flood            Disable stdout anti-flood\n");
    printf("  --quiet                  Disable all log output\n");
    printf("  --disable-network        Force-disable network backend\n");
    printf("  --screen-width=N         Override device cerf.json board.configurable_screen_width\n");
    printf("  --screen-height=N        Override device cerf.json board.configurable_screen_height\n");
    printf("  --start-window-width=N   Initial host window width before LCD enables (default 800)\n");
    printf("  --start-window-height=N  Initial host window height before LCD enables (default 600)\n");
    printf("  --poc-rom-injection      Replace explorer.exe at runtime with the\n");
    printf("                           bundled sampleapp.exe (PoC for ROM replacement)\n");
    printf("  --help                   Show this help\n");
    printf("\n");
    Log::PrintCategoryList();
}
