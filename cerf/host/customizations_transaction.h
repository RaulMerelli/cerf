#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

#include <string>

class CustomizationsTransaction : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    bool Open(HWND owner, bool force_reboot);

private:
    void Apply(std::string reboot);
};
