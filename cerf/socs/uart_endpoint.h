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

    /* Hibernation: an endpoint holding mutable guest-coupled state (e.g. the
       VMCU peer's IPC handshake phase) serializes it here; the owning UART
       forwards from its own Save/Restore. No-op default for stateless endpoints. */
    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
};
