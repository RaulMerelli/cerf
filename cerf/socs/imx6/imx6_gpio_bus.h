#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "../../core/service.h"
#include "../../state/state_stream.h"

class Imx6GpioInputSource;

/* Registry mediating i.MX6 GPIO banks and their board input sources. Banks and
   sources self-register in either order; the bus wires each source's Reevaluate
   hook to its bank's UpdateIrq once both are present. */
class Imx6GpioBus : public Service {
public:
    using Service::Service;

    void RegisterBank(uint32_t base, std::function<void()> reevaluate);
    void RegisterSource(Imx6GpioInputSource* source);
    Imx6GpioInputSource* Find(uint32_t base) const;

    /* Hibernation forwarding for one bank's source (registration order). */
    void SaveSources(uint32_t base, StateWriter& w) const;
    void RestoreSources(uint32_t base, StateReader& r) const;

private:
    struct Bank {
        uint32_t base;
        std::function<void()> reevaluate;
    };
    void Wire(uint32_t base);

    std::vector<Bank> banks_;
    std::vector<Imx6GpioInputSource*> sources_;
};
