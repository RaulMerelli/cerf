#pragma once
/* NullNetworkBackend - no-op NetworkBackend, selected when network_enabled=0.
   SendFrame drops; the receive callback is stored but never invoked. Lets
   the rest of the stack (NDIS miniport, ndis.dll, tcp/ip) come up cleanly
   on systems where outbound networking is intentionally disabled. */

#include "network_backend.h"

class NullNetworkBackend : public NetworkBackend {
public:
    using NetworkBackend::NetworkBackend;

    bool ShouldRegister() override;

    void SendFrame(const uint8_t* frame, std::size_t len) override;
    void SetReceiveCallback(RxFn cb) override;
    std::array<uint8_t, 6> GuestMacAddress() const override;
    std::array<uint8_t, 6> HostGatewayMacAddress() const override;

private:
    RxFn rx_cb_;
    std::array<uint8_t, 6> guest_mac_{};
    bool tx_logged_once_ = false;
};
