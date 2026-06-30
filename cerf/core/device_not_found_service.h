#pragma once

#include "service.h"

/* Registers only when the resolved device has no bootable image on disk.
   EnsureFound() reports + exits; it is invoked by RuntimeUserErrorsService
   (device checked before board), not auto-run as OnReady. */
class DeviceNotFoundService : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void EnsureFound();

private:
    bool IsDevicePresent();
};
