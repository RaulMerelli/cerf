#include "imx6_gpio_bus.h"

#include "imx6_gpio_source.h"
#include "../../core/cerf_emulator.h"

REGISTER_SERVICE(Imx6GpioBus);

void Imx6GpioBus::RegisterBank(uint32_t base, std::function<void()> reevaluate) {
    banks_.push_back({base, std::move(reevaluate)});
    Wire(base);
}

void Imx6GpioBus::RegisterSource(Imx6GpioInputSource* source) {
    if (!source) return;
    sources_.push_back(source);
    Wire(source->GpioBase());
}

Imx6GpioInputSource* Imx6GpioBus::Find(uint32_t base) const {
    for (Imx6GpioInputSource* s : sources_)
        if (s->GpioBase() == base) return s;
    return nullptr;
}

void Imx6GpioBus::Wire(uint32_t base) {
    Imx6GpioInputSource* s = Find(base);
    if (!s) return;
    for (const Bank& b : banks_)
        if (b.base == base && b.reevaluate)
            s->SetReevaluate(b.reevaluate);
}

void Imx6GpioBus::SaveSources(uint32_t base, StateWriter& w) const {
    for (Imx6GpioInputSource* s : sources_)
        if (s->GpioBase() == base) s->SaveState(w);
}

void Imx6GpioBus::RestoreSources(uint32_t base, StateReader& r) const {
    for (Imx6GpioInputSource* s : sources_)
        if (s->GpioBase() == base) s->RestoreState(r);
}
