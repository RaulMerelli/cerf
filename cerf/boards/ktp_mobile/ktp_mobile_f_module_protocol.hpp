#pragma once

#include "ktp_mobile_f_module_firmware.hpp"

namespace ktp_mobile::detail {

std::uint16_t NextRelaySequence(std::uint16_t current) noexcept {
    return current == 0xFFFFu ? 2u
                              : static_cast<std::uint16_t>(current + 1u);
}

const CommandSchema* FindSchema(const CommandSchema* begin,
                                const CommandSchema* end,
                                std::uint16_t command) noexcept {
    for (const CommandSchema* schema = begin; schema != end; ++schema) {
        if (schema->command == command) {
            return schema;
        }
    }
    return nullptr;
}

const CommandSchema* FindHostSchema(std::uint16_t command) noexcept {
    return FindSchema(kHostReachableSchemas.data(),
                      kHostReachableSchemas.data() + kHostReachableSchemas.size(),
                      command);
}

const CommandSchema* FindModuleSchema(std::uint16_t command) noexcept {
    return FindSchema(kModuleSchemas.data(),
                      kModuleSchemas.data() + kModuleSchemas.size(), command);
}

bool IsBoolByte(std::uint8_t value) noexcept {
    return value <= 1u;
}

bool IsValidResetKind(ResetKind value) noexcept {
    switch (value) {
        case ResetKind::Cold:
        case ResetKind::WarmModule:
            return true;
    }
    return false;
}

bool IsValidModulePhase(ModulePhase value) noexcept {
    switch (value) {
        case ModulePhase::Startup:
        case ModulePhase::Service:
        case ModulePhase::Fault:
        case ModulePhase::Bootloader:
            return true;
    }
    return false;
}

bool IsValidUpdatePhase(UpdatePhase value) noexcept {
    switch (value) {
        case UpdatePhase::Inactive:
        case UpdatePhase::EntryRequested:
        case UpdatePhase::TargetAccepted:
        case UpdatePhase::Receiving:
        case UpdatePhase::Finalizing:
        case UpdatePhase::Complete:
        case UpdatePhase::Aborted:
            return true;
    }
    return false;
}

bool IsSupportedSpiFormat(const SpiTransferFormat& format) noexcept {
    return format.bits_per_word == 8u &&
           format.clock_polarity == SpiClockPolarity::IdleLow &&
           format.clock_phase == SpiClockPhase::CaptureFirstEdge &&
           format.bit_order == SpiBitOrder::MsbFirst &&
           format.byte_order == SpiByteOrder::MostSignificantByteFirst;
}

bool AllZero(const std::uint8_t* data, std::size_t length) noexcept {
    for (std::size_t i = 0; i < length; ++i) {
        if (data[i] != 0u) {
            return false;
        }
    }
    return true;
}

void RecomputeReady(State& state) noexcept {
    if (state.chip_select_asserted != 0u || state.gpio6_ack != 0u) {
        state.gpio5_ready = 0u;
        return;
    }
    /* READY is also the cyclic-presence handshake.  The fixed host starts a
       new exchange within one second after ACK falls even when no relay record
       is queued; leaving READY low is reported as a missing F-module. */
    state.gpio5_ready = 1u;
}

void RefreshModuleStatus(State& state) noexcept {
    const std::uint8_t low_state =
        static_cast<std::uint8_t>(state.module_status_byte & 0x03u);
    state.module_status_byte = static_cast<std::uint8_t>(kStatusBase | low_state);
    if (state.startup_control_acknowledged != 0u) {
        state.module_status_byte =
            static_cast<std::uint8_t>(state.module_status_byte | kStartupAckBit);
    }
}

void ClearPhysicalTransaction(State& state) noexcept {
    state.chip_select_asserted = 0u;
    state.spi_bytes_transferred = 0u;
    state.reserved_spi = 0u;
    state.spi_rx.fill(0u);
    state.spi_tx.fill(0u);
}

void ClearActiveRelay(State& state) noexcept {
    state.active_module_relay_sequence = 0u;
    state.active_module_relay_length = 0u;
    state.active_module_relay.fill(0u);
}

void ClearStagedRecords(State& state) noexcept {
    state.staged_module_record_bytes = 0u;
    state.reserved_relay = 0u;
    state.staged_module_records.fill(0u);
}

void ResetRelayState(State& state) noexcept {
    state.next_module_relay_sequence = 1u;
    state.last_panel_relay_sequence = 0u;
    ClearActiveRelay(state);
    ClearStagedRecords(state);
}

void PromoteStagedRelay(State& state) noexcept {
    if (state.active_module_relay_length != 0u ||
        state.staged_module_record_bytes == 0u) {
        return;
    }

    const std::size_t record_bytes = state.staged_module_record_bytes;
    const std::size_t total_length =
        kRelayHeaderBytes + record_bytes + kRelayCrcBytes;
    state.active_module_relay.fill(0u);
    WriteBe16(state.active_module_relay.data(), state.next_module_relay_sequence);
    WriteBe32(state.active_module_relay.data() + 2u,
              static_cast<std::uint32_t>(total_length));
    std::copy_n(state.staged_module_records.data(), record_bytes,
                state.active_module_relay.data() + kRelayHeaderBytes);
    const std::uint16_t crc =
        Crc16(state.active_module_relay.data(), total_length - kRelayCrcBytes);
    WriteBe16(state.active_module_relay.data() + total_length - kRelayCrcBytes,
              crc);
    state.active_module_relay_sequence = state.next_module_relay_sequence;
    state.active_module_relay_length =
        static_cast<std::uint16_t>(total_length);
    ClearStagedRecords(state);
}


Status QueueModuleRecord(State& state,
                         std::uint16_t command,
                         const std::uint8_t* payload,
                         std::size_t payload_length) noexcept {
    const CommandSchema* schema = FindModuleSchema(command);
    if (schema == nullptr || schema->payload_length != payload_length ||
        payload_length > kRelayRecordBytes - kRecordHeaderBytes) {
        return Status::ProtocolRejected;
    }
    const std::size_t record_length = kRecordHeaderBytes + payload_length;
    if (record_length > kRelayRecordBytes - state.staged_module_record_bytes) {
        return Status::QueueFull;
    }

    std::uint8_t* record =
        state.staged_module_records.data() + state.staged_module_record_bytes;
    WriteBe16(record, command);
    WriteBe32(record + 2u, static_cast<std::uint32_t>(payload_length));
    if (payload_length != 0u) {
        std::copy_n(payload, payload_length, record + kRecordHeaderBytes);
    }
    state.staged_module_record_bytes = static_cast<std::uint16_t>(
        state.staged_module_record_bytes + record_length);
    PromoteStagedRelay(state);
    return Status::Ok;
}

bool ActiveRelayIsSuccessfulUpdateResponse(const State& state) noexcept {
    if (state.active_module_relay_length < 20u) {
        return false;
    }
    const std::uint8_t* record = state.active_module_relay.data() + kRelayHeaderBytes;
    return ReadBe16(record) == 239u && ReadBe32(record + 2u) == 6u &&
           record[kRecordHeaderBytes + 1u] == 17u;
}

Status QueueInstalledVersion(State& state) noexcept {
    if (state.firmware.valid == 0u) {
        return Status::InvalidState;
    }
    return QueueModuleRecord(state, 136u, state.firmware.version.data(),
                             state.firmware.version.size());
}

void BuildResponseFrame(const State& state,
                        std::array<std::uint8_t, kWireTransactionBytes>& out) noexcept {
    out.fill(0u);
    out[0] = kModuleMarker;
    std::uint8_t* logical = out.data() + 1u;

    WriteBe16(logical + kOuterSequenceOffset, state.last_panel_relay_sequence);
    std::copy(state.module_cyclic_bytes.begin(), state.module_cyclic_bytes.end(),
              logical + kCyclicOffset);
    logical[kStatusOffset] = state.module_status_byte;
    WriteBe16(logical + kOuterCrcOffset, Crc16(logical, 13u));

    if (state.active_module_relay_length != 0u) {
        std::copy_n(state.active_module_relay.data(), kRelayAreaBytes,
                    logical + kRelayOffset);
    }
}

bool ValidateRecordArea(const std::uint8_t* records,
                        std::size_t record_bytes,
                        bool host_to_module,
                        bool allow_update_command) noexcept {
    std::size_t cursor = 0u;
    while (cursor < record_bytes) {
        if (record_bytes - cursor < kRecordHeaderBytes) {
            return false;
        }
        const std::uint16_t command = ReadBe16(records + cursor);
        const std::uint32_t payload_length = ReadBe32(records + cursor + 2u);
        const std::size_t remaining = record_bytes - cursor - kRecordHeaderBytes;
        if (payload_length > remaining) {
            return false;
        }

        const CommandSchema* schema =
            host_to_module ? FindHostSchema(command) : FindModuleSchema(command);
        if (schema == nullptr || schema->payload_length != payload_length) {
            return false;
        }
        if (host_to_module && command == 239u && !allow_update_command) {
            return false;
        }

        cursor += kRecordHeaderBytes + static_cast<std::size_t>(payload_length);
    }
    return cursor == record_bytes;
}

bool ValidateActiveRelay(const State& state) noexcept {
    if (state.active_module_relay_length == 0u) {
        return state.active_module_relay_sequence == 0u &&
               AllZero(state.active_module_relay.data(),
                       state.active_module_relay.size());
    }
    if (state.active_module_relay_length < 8u ||
        state.active_module_relay_length > kRelayAreaBytes ||
        state.active_module_relay_sequence == 0u) {
        return false;
    }
    const std::size_t length = state.active_module_relay_length;
    if (ReadBe16(state.active_module_relay.data()) !=
            state.active_module_relay_sequence ||
        ReadBe32(state.active_module_relay.data() + 2u) != length) {
        return false;
    }
    const std::uint16_t stored_crc =
        ReadBe16(state.active_module_relay.data() + length - kRelayCrcBytes);
    if (stored_crc !=
        Crc16(state.active_module_relay.data(), length - kRelayCrcBytes)) {
        return false;
    }
    if (!ValidateRecordArea(state.active_module_relay.data() + kRelayHeaderBytes,
                            length - kRelayHeaderBytes - kRelayCrcBytes,
                            false, true)) {
        return false;
    }
    return AllZero(state.active_module_relay.data() + length,
                   state.active_module_relay.size() - length);
}

bool ValidateStagedRecords(const State& state) noexcept {
    if (state.staged_module_record_bytes > kRelayRecordBytes) {
        return false;
    }
    if (!ValidateRecordArea(state.staged_module_records.data(),
                            state.staged_module_record_bytes, false, true)) {
        return false;
    }
    return AllZero(state.staged_module_records.data() +
                       state.staged_module_record_bytes,
                   state.staged_module_records.size() -
                       state.staged_module_record_bytes);
}

bool ValidateFirmwareInfo(const FirmwareInfo& info) noexcept {
    if (!IsBoolByte(info.valid) || !IsBoolByte(info.flash_materialized) ||
        info.reserved != 0u || info.container_size > kMaxUpdateContainerBytes ||
        info.payload_size > kApplicationFlashBytes) {
        return false;
    }
    if (info.flash_materialized != 0u && info.valid == 0u) {
        return false;
    }
    if (info.valid == 0u &&
        (info.flash_materialized != 0u || info.container_size != 0u ||
         info.payload_size != 0u ||
         !AllZero(info.version.data(), info.version.size()))) {
        return false;
    }
    return true;
}


Status QueueUpdateResponse(State& state,
                           std::uint8_t status,
                           std::uint32_t request_sequence) noexcept {
    std::array<std::uint8_t, kFirmwareUpdateResponseBytes> response{};
    response[1] = status;
    WriteBe32(response.data() + 2u, request_sequence);
    const Status queued =
        QueueModuleRecord(state, 239u, response.data(), response.size());
    if (queued == Status::Ok) {
        state.update_last_wire_status = status;
    }
    return queued;
}

Status RejectUpdate(State& state,
                    std::uint32_t request_sequence,
                    bool final_seen) noexcept {
    state.update_phase = UpdatePhase::Aborted;
    if (state.module_phase != ModulePhase::Fault) {
        state.module_phase = ModulePhase::Service;
    }
    state.update_final_seen = final_seen ? 1u : 0u;
    return QueueUpdateResponse(state, 2u, request_sequence);
}

Status ProcessUpdateRequest(State& state,
                            const std::uint8_t* payload) noexcept {
    if (kRecordHeaderBytes + kFirmwareUpdateResponseBytes >
        kRelayRecordBytes - state.staged_module_record_bytes) {
        return Status::QueueFull;
    }

    const std::uint8_t opcode = payload[0];
    const std::uint8_t final_flag = payload[1];
    const std::uint32_t sequence = ReadBe32(payload + 2u);
    const std::uint16_t data_length = ReadBe16(payload + 6u);
    if (final_flag > 1u || data_length > kFirmwareUpdateBlockBytes) {
        return RejectUpdate(state, sequence, final_flag != 0u);
    }
    const std::uint8_t* data = payload + 8u;

    if (opcode == 9u) {
        if (final_flag != 0u || data_length != 0u || sequence != 0u ||
            state.update_phase != UpdatePhase::Inactive ||
            state.module_phase == ModulePhase::Fault) {
            return RejectUpdate(state, sequence, final_flag != 0u);
        }
        state.module_phase = ModulePhase::Bootloader;
        state.update_phase = UpdatePhase::EntryRequested;
        state.update_expected_sequence = 1u;
        state.update_staging_size = 0u;
        state.update_final_seen = 0u;
        state.update_target.fill(0u);
        state.update_staging.fill(0u);
        return QueueUpdateResponse(state, 1u, sequence);
    }

    if (opcode != 4u) {
        return RejectUpdate(state, sequence, final_flag != 0u);
    }

    if (final_flag != 0u) {
        if (data_length != 0u || sequence != 0u ||
            state.update_phase != UpdatePhase::Receiving) {
            return RejectUpdate(state, sequence, true);
        }
        state.update_phase = UpdatePhase::Finalizing;
        state.update_final_seen = 1u;
        if (!ValidateContainerStructure(state.update_staging.data(),
                                        state.update_staging_size) ||
            state.approved_container_valid == 0u ||
            Sha256(state.update_staging.data(), state.update_staging_size) !=
                state.approved_container_sha256) {
            return RejectUpdate(state, sequence, true);
        }

        state.application_flash.fill(0xFFu);
        std::copy_n(state.update_staging.data() + kUpdateContainerPrefixBytes,
                    kKnownPayloadBytes, state.application_flash.data());
        state.firmware.valid = 1u;
        state.firmware.flash_materialized = 1u;
        state.firmware.reserved = 0u;
        state.firmware.container_size = kKnownContainerBytes;
        state.firmware.payload_size = kKnownPayloadBytes;
        std::copy_n(state.update_staging.data() + kKnownVersionOffset,
                    state.firmware.version.size(), state.firmware.version.begin());
        state.update_phase = UpdatePhase::Complete;
        state.module_phase = ModulePhase::Service;
        state.update_expected_sequence = 0u;
        return QueueUpdateResponse(state, 17u, sequence);
    }

    if (state.update_phase == UpdatePhase::EntryRequested) {
        if (sequence != state.update_expected_sequence || data_length != 6u) {
            return RejectUpdate(state, sequence, false);
        }
        std::copy_n(data, state.update_target.size(), state.update_target.begin());
        if (!std::equal(kUpdateTarget.begin(), kUpdateTarget.end(), data)) {
            return RejectUpdate(state, sequence, false);
        }
        state.update_phase = UpdatePhase::TargetAccepted;
        state.update_expected_sequence = sequence + 1u;
        return QueueUpdateResponse(state, 1u, sequence);
    }

    if (state.update_phase != UpdatePhase::TargetAccepted &&
        state.update_phase != UpdatePhase::Receiving) {
        return RejectUpdate(state, sequence, false);
    }
    if (sequence != state.update_expected_sequence || data_length == 0u ||
        state.update_expected_sequence == std::numeric_limits<std::uint32_t>::max()) {
        return RejectUpdate(state, sequence, false);
    }
    if (data_length > kMaxUpdateContainerBytes - state.update_staging_size) {
        return RejectUpdate(state, sequence, false);
    }

    std::copy_n(data, data_length,
                state.update_staging.data() + state.update_staging_size);
    state.update_staging_size += data_length;
    state.update_expected_sequence = sequence + 1u;
    state.update_phase = UpdatePhase::Receiving;
    return QueueUpdateResponse(state, 1u, sequence);
}

Status DispatchIncomingRecords(State& state, const std::uint8_t* relay) noexcept {
    if (AllZero(relay, kRelayHeaderBytes)) {
        return Status::Ok;
    }
    const std::size_t relay_length = ReadBe32(relay + 2u);
    const std::size_t record_bytes = relay_length - kRelayHeaderBytes - kRelayCrcBytes;
    std::size_t cursor = 0u;
    const std::uint8_t* records = relay + kRelayHeaderBytes;
    while (cursor < record_bytes) {
        const std::uint16_t command = ReadBe16(records + cursor);
        const std::uint32_t payload_length = ReadBe32(records + cursor + 2u);
        const std::uint8_t* payload = records + cursor + kRecordHeaderBytes;
        if (command == 131u) {
            if (state.firmware.valid == 0u) {
                return Status::InvalidState;
            }
            /* The panel's compatibility query carries ConnBox ID/type.  The
               fixed module replies with its four-byte compatibility state. */
            const std::array<std::uint8_t, 4> compatibility{{0u, 0u, 0u, 0u}};
            const Status status = QueueModuleRecord(
                state, 131u, compatibility.data(), compatibility.size());
            if (status != Status::Ok) {
                return status;
            }
            const Status version_status = QueueInstalledVersion(state);
            if (version_status != Status::Ok) {
                return version_status;
            }
        } else if (command == 239u) {
            const Status status = ProcessUpdateRequest(state, payload);
            if (status != Status::Ok) {
                return status;
            }
        }
        cursor += kRecordHeaderBytes + static_cast<std::size_t>(payload_length);
    }
    return Status::Ok;
}



}  // namespace ktp_mobile::detail
