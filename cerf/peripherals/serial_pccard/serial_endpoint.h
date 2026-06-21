#pragma once

#include <cstddef>
#include <cstdint>

class Serial16550;

/* A serial "personality" behind a 16550 UART: consumes the guest's TX bytes,
   reacts to DTR/RTS changes, and pushes RX bytes + modem-status lines back
   through the bound Serial16550. Concretes: modem (AT), GPS (NMEA), raw bridge.
   NOT a Service - one per card, owned by SerialPcCard. */
class SerialEndpoint {
public:
    virtual ~SerialEndpoint() = default;

    /* Bound once by the card before any traffic; the personality keeps the
       reference to push RX bytes and assert modem-status lines. */
    void BindUart(Serial16550& uart) { uart_ = &uart; }

    /* Guest wrote `n` bytes to THR (already de-FIFO'd by the UART). The modem
       parses these as AT / forwards them over PPP; GPS ignores them. */
    virtual void OnGuestTx(const uint8_t* data, size_t n) = 0;

    /* Guest changed the MCR control outputs. The modem treats a DTR 1->0 edge
       as hang-up; a personality that does not care leaves this default. */
    virtual void OnControlLines(bool dtr, bool rts) { (void)dtr; (void)rts; }

    /* Card power / socket-reset lifecycle. */
    virtual void OnOpen()  {}
    virtual void OnClose() {}

protected:
    Serial16550* uart_ = nullptr;
};
