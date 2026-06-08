#pragma once

#include "../core/service.h"

#include <string>

/* Staged path of a guest-additions ce_apps ARM binary for the current guest
   CPU: Thumb cores get the _thumb interworking build, no-Thumb cores the
   pure-ARM build. */
class GuestAdditionsBinaries : public Service {
public:
    using Service::Service;

    std::string StagedPath(const std::string& dll_name);
};
