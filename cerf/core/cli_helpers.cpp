#include "cli_helpers.h"
#include "log.h"
#include <cstdio>

void PrintUsage(const char* prog) {
    printf("CERF - Windows CE virtual platform\n");
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
    printf("  --screen-dpi=N           Force guest display DPI (logical px/inch; guest-additions only)\n");
    printf("  --guest-additions        Inject CERF guest-additions DLL into the ROM\n");
    printf("                           (replaces matching modules with CERF-built equivalents)\n");
    printf("  --share-folder=PATH      Pre-enable the guest-additions shared folder on a host\n");
    printf("                           directory at boot (requires --guest-additions)\n");
    printf("  --recovery               Boot the device's recovery ROM (rom.recovery) instead of primary\n");
    printf("  --boot=resume|cold|warm  Saved-state boot action when state.img exists\n");
    printf("                           (resume=full restore, warm=RAM+flash only, cold=ignore)\n");
    printf("  --boot-anim=enable|disable  HwScreen logo boot animation (default: off in dev, on in release)\n");
    printf("  --full-screen            Enter borderless fullscreen (Right Ctrl+F) once the window is shown\n");
    printf("  --help                   Show this help\n");
    printf("\n");
    Log::PrintCategoryList();
}
