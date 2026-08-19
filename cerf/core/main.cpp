#include "log.h"
#include "main_config.h"
#include "cerf_emulator.h"
#include "../version.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>

int main(int argc, char* argv[]) {
    CerfConfig cfg;
    switch (ParseCerfArgs(argc, argv, cfg)) {
        case ArgParseResult::Run:         break;
        case ArgParseResult::HelpShown:   return CERF_FATAL_NORMAL_EXIT;
        case ArgParseResult::BadArgument: return CERF_FATAL_USER_ERROR;
    }

    /* Without this, sub-ms cv_.wait_for/Sleep round to the 15.625 ms
       Windows default quantum - OST IRQ latency breaks audio + UI. */
    timeBeginPeriod(1);

    Log::InitDefaultLogFile();
    Log::InstallCrashHandler();

    LOG(Cerf, "== CE Runtime Foundation %s ==\n", CERF_VERSION_DISPLAY_STR);
    LOG(Cerf, "main.cpp compiled at: %s %s\n\n", __DATE__, __TIME__);

    CerfEmulator emu(cfg, argc, argv);
    emu.Boot();
    emu.WaitForExit();

    Log::Close();
    timeEndPeriod(1);
    return 0;
}
