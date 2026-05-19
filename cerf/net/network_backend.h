#pragma once

#include "../core/service.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

class NetworkBackend : public Service {
public:
    using Service::Service;

    using RxFn = std::function<void(const uint8_t* frame, std::size_t len)>;

    virtual void SendFrame(const uint8_t* frame, std::size_t len) = 0;
    virtual void SetReceiveCallback(RxFn cb) = 0;

    virtual std::array<uint8_t, 6> GuestMacAddress() const = 0;
    virtual std::array<uint8_t, 6> HostGatewayMacAddress() const = 0;
};
