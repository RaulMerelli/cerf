#pragma once

#include <cstdint>

class StateWriter;
class StateReader;

/* Off-chip device on a UART's serial lines (e.g. the SYNC2 VMCU companion on
   UART2). The UART forwards guest TX bytes here; the endpoint replies via the
   UART's InjectRx(). */
class UartEndpoint {
public:
    virtual ~UartEndpoint() = default;

    /* Called on the guest (JIT) thread when the guest writes UTXD. */
    virtual void OnGuestTx(uint8_t byte) = 0;

    /* Called when the guest writes a UART control register (aligned offset +
       merged value), so an endpoint can time behaviour to port config — e.g.
       inject an initial RX byte once the driver enables the receiver. No-op
       default for endpoints that only react to TX. */
    virtual void OnControlWrite(uint32_t reg_off, uint32_t value) {
        (void)reg_off;
        (void)value;
    }

    /* Hibernation: an endpoint holding mutable guest-coupled state (e.g. the
       VMCU peer's IPC handshake phase) serializes it here; the owning UART
       forwards from its own Save/Restore. No-op default for stateless endpoints. */
    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
};
