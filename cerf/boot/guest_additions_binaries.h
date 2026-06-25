#pragma once

#include "../core/service.h"

#include <string>

/* Full staged paths of the guest-additions CE binaries for the current guest
   CPU. The body + stub DLL names are owned here; the only per-guest variable is
   the ce_apps build-output arch directory (arm / arm_thumb / mips). */
class GuestAdditionsBinaries : public Service {
public:
    using Service::Service;

    std::string BodyPath();   /* cerf_guest.dll */
    std::string StubPath();   /* cerf_guest_stub.dll */

private:
    std::string ArchDir();
};
