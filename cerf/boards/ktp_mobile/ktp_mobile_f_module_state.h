#pragma once

#include "ktp_mobile_f_module_model.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ktp_mobile::detail {

struct IncomingRelay {
    bool present = false;
    bool duplicate = false;
    std::uint16_t sequence = 0u;
};

bool ValidateSnapshot(const State& state) noexcept;
void ResetVolatile(State& state, ResetKind kind) noexcept;
Status ValidateIncomingRelay(const std::uint8_t* relay, const State& state,
                             IncomingRelay& parsed) noexcept;
void FinalizeAdvertisedResponse(
    State& state,
    const std::array<std::uint8_t, kWireTransactionBytes>& response,
    bool request_was_logically_valid) noexcept;
Status CommitWireRequest(
    State& state,
    const std::array<std::uint8_t, kWireTransactionBytes>& request,
    const std::array<std::uint8_t, kWireTransactionBytes>& response) noexcept;

}
