#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "../../core/service.h"
#include "../../state/state_stream.h"

class Imx6GpioInputSource;

class Imx6GpioBus : public Service {
public:
    using Service::Service;

    void RegisterBank(uint32_t base, std::function<void()> reevaluate);
    void RegisterSource(Imx6GpioInputSource* source);
    Imx6GpioInputSource* Find(uint32_t base) const;

    void SaveSources(uint32_t base, StateWriter& w) const;
    void RestoreSources(uint32_t base, StateReader& r) const;
    void PostRestoreSources(uint32_t base) const;

private:
    struct Bank {
        uint32_t base;
        std::function<void()> reevaluate;
    };
    void Wire(uint32_t base);

    std::vector<Bank> banks_;
    std::vector<Imx6GpioInputSource*> sources_;
};
