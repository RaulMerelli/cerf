#pragma once

#include <cstdint>
#include <optional>

#include "../core/service.h"

/* The guest CPU engine JitRunner drives. ArmJit and MipsJit implement it; the
   active engine registers itself in OnReady (Provide<GuestEngine>) and JitRunner
   resolves Get<GuestEngine>(). */
class GuestEngine : public Service {
public:
    using Service::Service;

    virtual void     Run()                = 0;
    virtual bool     DeepSleep()    const = 0;
    virtual bool     ResetPending() const = 0;
    virtual uint32_t Pc()           const = 0;
    virtual void     DispatchTraceIter()  = 0;

    virtual std::optional<uint8_t*> PeekGuestVa(uint32_t va) = 0;
};
