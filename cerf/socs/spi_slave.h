#pragma once

#include "../core/service.h"

#include <cstdint>

class StateWriter;
class StateReader;

/* An SPI slave device on a (host-modelled) eCSPI bus. The controller shifts
   bytes full-duplex: each Exchange() shifts one byte out (MOSI) and the slave
   shifts one byte back (MISO). Used by the i.MX51 eCSPI controllers; the slave
   self-attaches to its controller in OnReady. */
class SpiSlave : public Service {
public:
    using Service::Service;
    ~SpiSlave() override = default;

    /* An SpiSlave is a Service, not a Peripheral, so it is absent from the
       hibernation peripheral walk; the owning eCSPI controller forwards these
       from its own SaveState/RestoreState (hibernation.md). Default no-op. */
    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}

    /* Full-duplex byte exchange: the controller shifts `mosi` out and the slave
       returns the byte it shifts back on MISO. */
    virtual uint8_t Exchange(uint8_t mosi) = 0;
};
