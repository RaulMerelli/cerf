#pragma once

#include "../../core/service.h"

#include <optional>
#include <string>

/* How one board wires the SIU's modem lines. CTS/DSR/RI reach the guest through the
   SIU's own SIUMS, but a board is free to route DCD elsewhere, so the pin it lands on
   is a per-board fact and never a default. */
struct Vr4102SerialModem {
    std::wstring label;          /* cradle label, e.g. L"COM1" */

    /* GIU pin carrying DCD, or -1 when DCD is the SIU's own SIUMS bit. Active low. */
    int dcd_giu_pin = -1;
};

class Vr4102SerialWiring : public Service {
public:
    using Service::Service;

    virtual std::optional<Vr4102SerialModem> ForSiu() const = 0;
};
