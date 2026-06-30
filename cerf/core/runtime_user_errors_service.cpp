#include "board_not_found_service.h"
#include "device_not_found_service.h"
#include "cerf_emulator.h"
#include "service.h"

namespace {

class RuntimeUserErrorsService : public Service {
public:
    using Service::Service;

    void OnReady() override {
        if (auto* d = emu_.TryGet<DeviceNotFoundService>()) d->EnsureFound();
        if (auto* b = emu_.TryGet<BoardNotFoundService>())  b->EnsureFound();
    }
};

}  /* namespace */

REGISTER_SERVICE(RuntimeUserErrorsService);
