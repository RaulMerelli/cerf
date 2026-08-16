#include "fatal.h"

#include "cerf_emulator.h"
#include "log.h"
#include "../jit/guest_engine.h"

#include <cstdarg>
#include <cstdio>

REGISTER_SERVICE(Fatal);

void Fatal::Die(const char* fmt, ...) {
    char reason[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(reason, sizeof(reason), fmt, ap);
    va_end(ap);

    LOG(Jit, "%s\n", reason);

    if (GuestEngine* engine = live_engine_.load(std::memory_order_acquire))
        engine->PrintFatalDump();
    else
        LOG(Jit, "no guest context: the engine is not live\n");

    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}
