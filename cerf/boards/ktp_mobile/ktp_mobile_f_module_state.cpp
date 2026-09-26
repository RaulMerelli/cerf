#include "ktp_mobile_f_module_state.h"

#include "ktp_mobile_f_module_firmware.h"
#include "ktp_mobile_f_module_protocol.h"

#include <algorithm>

namespace ktp_mobile::detail {

bool ValidateUpdateState(const State& state) noexcept {
    if (state.update_staging_size > kMaxUpdateContainerBytes ||
        !AllZero(state.update_staging.data() + state.update_staging_size,
                 state.update_staging.size() - state.update_staging_size)) {
        return false;
    }

    const bool target_zero =
        AllZero(state.update_target.data(), state.update_target.size());
    const bool target_known =
        std::equal(kUpdateTarget.begin(), kUpdateTarget.end(),
                   state.update_target.begin());

    switch (state.update_phase) {
        case UpdatePhase::Inactive:
            return state.module_phase != ModulePhase::Bootloader &&
                   state.update_expected_sequence == 0u &&
                   state.update_staging_size == 0u &&
                   state.update_last_wire_status == 0u &&
                   state.update_final_seen == 0u && target_zero;
        case UpdatePhase::EntryRequested:
            return state.module_phase == ModulePhase::Bootloader &&
                   state.update_expected_sequence == 1u &&
                   state.update_staging_size == 0u &&
                   state.update_last_wire_status == 1u &&
                   state.update_final_seen == 0u && target_zero;
        case UpdatePhase::TargetAccepted:
            return state.module_phase == ModulePhase::Bootloader &&
                   state.update_expected_sequence == 2u &&
                   state.update_staging_size == 0u &&
                   state.update_last_wire_status == 1u &&
                   state.update_final_seen == 0u && target_known;
        case UpdatePhase::Receiving:
            return state.module_phase == ModulePhase::Bootloader &&
                   state.update_expected_sequence >= 3u &&
                   state.update_staging_size != 0u &&
                   state.update_last_wire_status == 1u &&
                   state.update_final_seen == 0u && target_known;
        case UpdatePhase::Finalizing:
            return false;
        case UpdatePhase::Complete: {
            ParsedContainer parsed{};
            if (!ParseContainerStructure(state.update_staging.data(),
                                         state.update_staging_size, parsed)) {
                return false;
            }
            return state.module_phase == ModulePhase::Service &&
                   state.update_expected_sequence == 0u &&
                   state.update_last_wire_status == 17u &&
                   state.update_final_seen == 1u && target_known &&
                   state.firmware.valid != 0u &&
                   state.firmware.container_size == state.update_staging_size &&
                   state.firmware.payload_size == parsed.payload_size &&
                   std::equal(state.firmware.version.begin(),
                              state.firmware.version.end(),
                              state.update_staging.data() + parsed.version_offset) &&
                   state.approved_container_valid != 0u &&
                   Sha256(state.update_staging.data(), state.update_staging_size) ==
                       state.approved_container_sha256;
        }
        case UpdatePhase::Aborted:
            return state.module_phase != ModulePhase::Bootloader &&
                   state.update_last_wire_status == 2u;
    }
    return false;
}

bool ValidateSnapshot(const State& state) noexcept {
    if (!IsValidResetKind(state.last_reset) ||
        !IsValidModulePhase(state.module_phase) ||
        !IsValidUpdatePhase(state.update_phase)) {
        return false;
    }

    if (!IsBoolByte(state.gpio5_ready) || !IsBoolByte(state.gpio6_ack) ||
        !IsBoolByte(state.chip_select_asserted) ||
        !IsBoolByte(state.startup_exchange_pending) ||
        !IsBoolByte(state.startup_control_acknowledged) ||
        !IsBoolByte(state.update_final_seen)) {
        return false;
    }

    if (state.reserved_spi != 0u || state.reserved_outer != 0u ||
        state.reserved_relay != 0u) {
        return false;
    }

    if (state.spi_bytes_transferred > kWireTransactionBytes) {
        return false;
    }
    if (state.chip_select_asserted == 0u) {
        if (state.spi_bytes_transferred != 0u ||
            !AllZero(state.spi_rx.data(), state.spi_rx.size()) ||
            !AllZero(state.spi_tx.data(), state.spi_tx.size())) {
            return false;
        }
    } else {
        if (state.gpio5_ready == 0u) {
            return false;
        }
        if (!AllZero(state.spi_rx.data() + state.spi_bytes_transferred,
                     state.spi_rx.size() - state.spi_bytes_transferred)) {
            return false;
        }
        std::array<std::uint8_t, kWireTransactionBytes> expected{};
        BuildResponseFrame(state, expected);
        if (expected != state.spi_tx) {
            return false;
        }
    }

    if (state.chip_select_asserted == 0u && state.gpio6_ack != 0u &&
        state.gpio5_ready != 0u) {
        return false;
    }
    if (state.chip_select_asserted == 0u && state.gpio6_ack == 0u &&
        state.gpio5_ready == 0u) {
        return false;
    }

    if (state.next_module_relay_sequence == 0u || !ValidateActiveRelay(state) ||
        !ValidateStagedRecords(state)) {
        return false;
    }
    if (state.active_module_relay_length == 0u &&
        state.staged_module_record_bytes != 0u) {
        return false;
    }
    if (state.active_module_relay_length != 0u &&
        state.active_module_relay_sequence != state.next_module_relay_sequence) {
        return false;
    }

    if (!ValidateFirmwareInfo(state.firmware)) {
        return false;
    }
    if (!IsBoolByte(state.approved_container_valid) ||
        !AllZero(state.reserved_approved.data(), state.reserved_approved.size()) ||
        (state.approved_container_valid == 0u &&
         !AllZero(state.approved_container_sha256.data(),
                  state.approved_container_sha256.size()))) {
        return false;
    }

    if (!ValidateUpdateState(state)) {
        return false;
    }

    return true;
}

void ResetVolatile(State& state, ResetKind kind) noexcept {
    state.last_reset = kind;
    state.module_phase = ModulePhase::Startup;
    state.update_phase = UpdatePhase::Inactive;

    state.gpio5_ready = 1u;
    state.gpio6_ack = 0u;
    state.chip_select_asserted = 0u;
    state.startup_exchange_pending = 1u;

    state.spi_bytes_transferred = 0u;
    state.reserved_spi = 0u;
    state.spi_rx.fill(0u);
    state.spi_tx.fill(0u);

    state.panel_cyclic_bytes.fill(0u);
    state.module_cyclic_bytes.fill(0u);
    state.panel_status_byte = 0u;
    state.module_status_byte = static_cast<std::uint8_t>(kStatusBase | 1u);
    state.startup_control_acknowledged = 0u;
    state.reserved_outer = 0u;

    ResetRelayState(state);

    state.update_expected_sequence = 0u;
    state.update_staging_size = 0u;
    state.update_last_wire_status = 0u;
    state.update_final_seen = 0u;
    state.update_target.fill(0u);
    state.update_staging.fill(0u);
}

Status ValidateIncomingRelay(const std::uint8_t* relay,
                             const State& state,
                             IncomingRelay& parsed) noexcept {
    parsed = IncomingRelay{};
    if (AllZero(relay, kRelayHeaderBytes)) {
        return Status::Ok;
    }

    const std::uint16_t sequence = ReadBe16(relay);
    const std::uint32_t length32 = ReadBe32(relay + 2u);
    if (sequence == 0u || length32 < 8u || length32 > kRelayAreaBytes) {
        return Status::ProtocolRejected;
    }
    const std::size_t length = static_cast<std::size_t>(length32);
    if (ReadBe16(relay + length - kRelayCrcBytes) !=
        Crc16(relay, length - kRelayCrcBytes)) {
        return Status::ProtocolRejected;
    }

    if (!ValidateRecordArea(relay + kRelayHeaderBytes,
                            length - kRelayHeaderBytes - kRelayCrcBytes,
                            true, true)) {
        return Status::ProtocolRejected;
    }

    parsed.sequence = sequence;
    if (state.last_panel_relay_sequence == 0u) {
        if (sequence != 1u) {
            return Status::ProtocolRejected;
        }
    } else if (sequence == state.last_panel_relay_sequence) {
        parsed.present = true;
        parsed.duplicate = true;
        parsed.sequence = sequence;
        return Status::Ok;
    } else if (sequence == 1u && state.last_panel_relay_sequence > 1u) {
        return Status::ProtocolRejected;
    } else if (sequence != NextRelaySequence(state.last_panel_relay_sequence)) {
        return Status::ProtocolRejected;
    }

    parsed.present = true;
    parsed.sequence = sequence;
    return Status::Ok;
}

void FinalizeAdvertisedResponse(State& state,
                                const std::array<std::uint8_t,
                                                 kWireTransactionBytes>& response,
                                bool request_was_logically_valid) noexcept {
    const std::uint8_t* logical = response.data() + 1u;
    const std::uint16_t advertised_ack = ReadBe16(logical);
    const bool advertised_startup_ack =
        (logical[kStatusOffset] & kStartupAckBit) != 0u;

    if (state.startup_exchange_pending != 0u) {
        if (state.last_panel_relay_sequence != 0u &&
            advertised_ack == state.last_panel_relay_sequence) {
            state.startup_exchange_pending = 0u;
        } else if (state.last_panel_relay_sequence == 0u &&
                   request_was_logically_valid) {
            state.startup_exchange_pending = 0u;
        }
    }

    if (advertised_startup_ack) {
        state.startup_control_acknowledged = 0u;
        if (state.module_phase == ModulePhase::Startup) {
            state.module_phase = ModulePhase::Service;
        }
    }
    RefreshModuleStatus(state);
}

Status CommitWireRequest(State& state,
                         const std::array<std::uint8_t,
                                          kWireTransactionBytes>& request,
                         const std::array<std::uint8_t,
                                          kWireTransactionBytes>& response) noexcept {
    if (request[0] != kPanelMarker) {
        FinalizeAdvertisedResponse(state, response, false);
        return Status::ProtocolRejected;
    }

    const std::uint8_t* logical = request.data() + 1u;
    if (ReadBe16(logical + kOuterCrcOffset) != Crc16(logical, 13u)) {
        FinalizeAdvertisedResponse(state, response, false);
        return Status::ProtocolRejected;
    }

    IncomingRelay incoming{};
    Status relay_status =
        ValidateIncomingRelay(logical + kRelayOffset, state, incoming);
    if (relay_status != Status::Ok && incoming.sequence == 1u &&
        state.last_panel_relay_sequence > 1u) {
        ResetRelayState(state);
        relay_status =
            ValidateIncomingRelay(logical + kRelayOffset, state, incoming);
    }
    if (relay_status != Status::Ok) {
        FinalizeAdvertisedResponse(state, response, false);
        return relay_status;
    }

    FinalizeAdvertisedResponse(state, response, true);

    std::copy_n(logical + kCyclicOffset, state.panel_cyclic_bytes.size(),
                state.panel_cyclic_bytes.begin());
    state.panel_status_byte = logical[kStatusOffset];

    const std::uint16_t outgoing_ack = ReadBe16(logical + kOuterSequenceOffset);
    if (state.active_module_relay_length != 0u &&
        outgoing_ack == state.active_module_relay_sequence) {
        const std::uint16_t acknowledged = state.active_module_relay_sequence;
        const bool completed_update =
            ActiveRelayIsSuccessfulUpdateResponse(state) &&
            state.update_phase == UpdatePhase::Complete;
        ClearActiveRelay(state);
        state.next_module_relay_sequence = NextRelaySequence(acknowledged);
        PromoteStagedRelay(state);
        if (completed_update && state.active_module_relay_length == 0u) {
            const Status version_status = QueueInstalledVersion(state);
            if (version_status != Status::Ok) {
                return version_status;
            }
        }
    }

    if (incoming.present && !incoming.duplicate) {
        const Status dispatch_status =
            DispatchIncomingRecords(state, logical + kRelayOffset);
        if (dispatch_status != Status::Ok) {
            return dispatch_status;
        }
        state.last_panel_relay_sequence = incoming.sequence;
        state.startup_exchange_pending = 1u;
    }

    const bool response_had_startup_ack =
        (response[1u + kStatusOffset] & kStartupAckBit) != 0u;
    if ((state.panel_status_byte & kStartupRequestBit) != 0u &&
        !response_had_startup_ack &&
        state.startup_control_acknowledged == 0u) {
        state.startup_control_acknowledged = 1u;
    }
    RefreshModuleStatus(state);
    return Status::Ok;
}


}
