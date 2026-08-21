#pragma once

#include "service.h"

enum class HostThreadRole {
    TimerExpiry,
    GuestCpu,
};

class HostThreadPriority : public Service {
public:
    using Service::Service;

    void OnReady() override;

    void Elevate(HostThreadRole role) const;

private:
    bool elevate_ = false;
};
