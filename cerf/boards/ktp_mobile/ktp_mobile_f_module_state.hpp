#pragma once

#include "ktp_mobile_f_module_protocol.hpp"

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
        case UpdatePhase::Complete:
            return state.module_phase == ModulePhase::Service &&
                   state.update_expected_sequence == 0u &&
                   state.update_staging_size == kKnownContainerBytes &&
                   state.update_last_wire_status == 17u &&
                   state.update_final_seen == 1u && target_known &&
                   state.firmware.valid != 0u &&
                   state.firmware.container_size == kKnownContainerBytes &&
                   state.firmware.payload_size == kKnownPayloadBytes &&
                   std::equal(state.firmware.version.begin(),
                              state.firmware.version.end(),
                              state.update_staging.data() + kKnownVersionOffset) &&
                   state.approved_container_valid != 0u &&
                   ValidateContainerStructure(state.update_staging.data(),
                                              state.update_staging_size) &&
                   Sha256(state.update_staging.data(), state.update_staging_size) ==
                       state.approved_container_sha256;
        case UpdatePhase::Aborted:
            return state.module_phase != ModulePhase::Bootloader &&
                   state.update_last_wire_status == 2u;
    }
    return false;
}

bool ValidateSnapshot(const State& state) noexcept {
    if (state.schema_version != kStateSchemaVersion ||
        !IsValidResetKind(state.last_reset) ||
        !IsValidModulePhase(state.module_phase) ||
        !IsValidUpdatePhase(state.update_phase)) {
        return false;
    }

    if (!IsBoolByte(state.gpio5_ready) || !IsBoolByte(state.gpio6_ack) ||
        !IsBoolByte(state.chip_select_asserted) ||
        !IsBoolByte(state.startup_exchange_pending) ||
        !IsBoolByte(state.startup_control_acknowledged) ||
        !IsBoolByte(state.fault_active) || !IsBoolByte(state.update_final_seen)) {
        return false;
    }

    if (state.reserved_spi != 0u || state.reserved_outer != 0u ||
        state.reserved_relay != 0u || state.reserved_fault0 != 0u) {
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

    if (state.fault_payload_size > state.fault_payload.size()) {
        return false;
    }
    if (!AllZero(state.fault_payload.data() + state.fault_payload_size,
                 state.fault_payload.size() - state.fault_payload_size)) {
        return false;
    }
    if (state.fault_active != 0u) {
        if (state.fault_payload_size < 2u || state.fault_payload[0] != 0xFDu ||
            state.fault_payload[1] != 0x01u ||
            state.module_phase != ModulePhase::Fault) {
            return false;
        }
    } else if (state.module_phase == ModulePhase::Fault) {
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
    state.schema_version = kStateSchemaVersion;
    state.last_reset = kind;
    state.module_phase = ModulePhase::Startup;
    state.update_phase = UpdatePhase::Inactive;

    state.gpio5_ready = 1u;
    state.gpio6_ack = 0u;
    state.chip_select_asserted = 0u;
    state.startup_exchange_pending = 1u;
    state.deterministic_time_us = 0u;

    state.spi_bytes_transferred = 0u;
    state.reserved_spi = 0u;
    state.spi_rx.fill(0u);
    state.spi_tx.fill(0u);

    state.panel_cyclic_bytes.fill(0u);
    state.module_cyclic_bytes.fill(0u);
    state.panel_status_byte = 0u;
    /* FModuleService decodes status[1:0] as a three-state BusRelay value;
       one is the quiescent state, while zero is diagnosed as FileID 16,
       line 438 on every cycle. */
    state.module_status_byte = static_cast<std::uint8_t>(kStatusBase | 1u);
    state.startup_control_acknowledged = 0u;
    state.reserved_outer = 0u;

    ResetRelayState(state);

    state.fault_active = 0u;
    state.reserved_fault0 = 0u;
    state.fault_payload_size = 0u;
    state.fault_payload.fill(0u);

    state.update_expected_sequence = 0u;
    state.update_staging_size = 0u;
    state.update_last_wire_status = 0u;
    state.update_final_seen = 0u;
    state.update_target.fill(0u);
    state.update_staging.fill(0u);
}

struct IncomingRelay {
    bool present = false;
    bool duplicate = false;
    std::uint16_t sequence = 0u;
};

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
        /* The response is built before this request is committed.  Until the
           panel receives the following response carrying its sequence as the
           outer ACK, it legitimately retransmits the same relay. */
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
    /* FModuleService can tear down and recreate its BusRelay session without
       resetting the physical controller.  A new, valid sequence 1 frame is
       the session boundary: discard the old relay window and validate the
       frame again as the first request of the new session. */
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


}  // namespace ktp_mobile::detail
