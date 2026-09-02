#include "ktp_mobile_f_module_device.h"

#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/cerf_paths.h"
#include "../../core/fatal.h"
#include "../../core/log.h"
#include "../../core/virtual_clock.h"
#include "../../cpu/emulated_memory.h"
#include "../../state/state_stream.h"
#include "../../peripherals/sd_card/fwf_fsf_container.h"

#include <cstring>
#include <fstream>
#include <memory>
#include <type_traits>
#include <vector>

namespace {

template <typename E>
void WriteEnum(StateWriter& w, E value) {
    w.Write(static_cast<std::underlying_type_t<E>>(value));
}

template <typename E>
void ReadEnum(StateReader& r, E& value) {
    std::underlying_type_t<E> raw{};
    r.Read(raw);
    value = static_cast<E>(raw);
}

void WriteModelState(StateWriter& w, const ktp_mobile::State& s) {
    w.Write(s.schema_version);
    WriteEnum(w, s.last_reset);
    WriteEnum(w, s.module_phase);
    WriteEnum(w, s.update_phase);
    w.Write(s.gpio5_ready);
    w.Write(s.gpio6_ack);
    w.Write(s.chip_select_asserted);
    w.Write(s.startup_exchange_pending);
    w.Write(s.deterministic_time_us);
    w.Write(s.spi_bytes_transferred);
    w.Write(s.reserved_spi);
    w.WriteBytes(s.spi_rx.data(), s.spi_rx.size());
    w.WriteBytes(s.spi_tx.data(), s.spi_tx.size());
    w.WriteBytes(s.panel_cyclic_bytes.data(), s.panel_cyclic_bytes.size());
    w.WriteBytes(s.module_cyclic_bytes.data(), s.module_cyclic_bytes.size());
    w.Write(s.panel_status_byte);
    w.Write(s.module_status_byte);
    w.Write(s.startup_control_acknowledged);
    w.Write(s.reserved_outer);
    w.Write(s.next_module_relay_sequence);
    w.Write(s.last_panel_relay_sequence);
    w.Write(s.active_module_relay_sequence);
    w.Write(s.active_module_relay_length);
    w.WriteBytes(s.active_module_relay.data(), s.active_module_relay.size());
    w.Write(s.staged_module_record_bytes);
    w.Write(s.reserved_relay);
    w.WriteBytes(s.staged_module_records.data(), s.staged_module_records.size());
    w.Write(s.fault_active);
    w.Write(s.reserved_fault0);
    w.Write(s.fault_payload_size);
    w.WriteBytes(s.fault_payload.data(), s.fault_payload.size());
    w.Write(s.firmware.valid);
    w.Write(s.firmware.flash_materialized);
    w.Write(s.firmware.reserved);
    w.Write(s.firmware.container_size);
    w.Write(s.firmware.payload_size);
    w.WriteBytes(s.firmware.version.data(), s.firmware.version.size());
    w.WriteBytes(s.application_flash.data(), s.application_flash.size());
    w.Write(s.approved_container_valid);
    w.WriteBytes(s.reserved_approved.data(), s.reserved_approved.size());
    w.WriteBytes(s.approved_container_sha256.data(), s.approved_container_sha256.size());
    w.Write(s.update_expected_sequence);
    w.Write(s.update_staging_size);
    w.Write(s.update_last_wire_status);
    w.Write(s.update_final_seen);
    w.WriteBytes(s.update_target.data(), s.update_target.size());
    w.WriteBytes(s.update_staging.data(), s.update_staging.size());
}

void ReadModelState(StateReader& r, ktp_mobile::State& s) {
    r.Read(s.schema_version);
    ReadEnum(r, s.last_reset);
    ReadEnum(r, s.module_phase);
    ReadEnum(r, s.update_phase);
    r.Read(s.gpio5_ready);
    r.Read(s.gpio6_ack);
    r.Read(s.chip_select_asserted);
    r.Read(s.startup_exchange_pending);
    r.Read(s.deterministic_time_us);
    r.Read(s.spi_bytes_transferred);
    r.Read(s.reserved_spi);
    r.ReadBytes(s.spi_rx.data(), s.spi_rx.size());
    r.ReadBytes(s.spi_tx.data(), s.spi_tx.size());
    r.ReadBytes(s.panel_cyclic_bytes.data(), s.panel_cyclic_bytes.size());
    r.ReadBytes(s.module_cyclic_bytes.data(), s.module_cyclic_bytes.size());
    r.Read(s.panel_status_byte);
    r.Read(s.module_status_byte);
    r.Read(s.startup_control_acknowledged);
    r.Read(s.reserved_outer);
    r.Read(s.next_module_relay_sequence);
    r.Read(s.last_panel_relay_sequence);
    r.Read(s.active_module_relay_sequence);
    r.Read(s.active_module_relay_length);
    r.ReadBytes(s.active_module_relay.data(), s.active_module_relay.size());
    r.Read(s.staged_module_record_bytes);
    r.Read(s.reserved_relay);
    r.ReadBytes(s.staged_module_records.data(), s.staged_module_records.size());
    r.Read(s.fault_active);
    r.Read(s.reserved_fault0);
    r.Read(s.fault_payload_size);
    r.ReadBytes(s.fault_payload.data(), s.fault_payload.size());
    r.Read(s.firmware.valid);
    r.Read(s.firmware.flash_materialized);
    r.Read(s.firmware.reserved);
    r.Read(s.firmware.container_size);
    r.Read(s.firmware.payload_size);
    r.ReadBytes(s.firmware.version.data(), s.firmware.version.size());
    r.ReadBytes(s.application_flash.data(), s.application_flash.size());
    r.Read(s.approved_container_valid);
    r.ReadBytes(s.reserved_approved.data(), s.reserved_approved.size());
    r.ReadBytes(s.approved_container_sha256.data(), s.approved_container_sha256.size());
    r.Read(s.update_expected_sequence);
    r.Read(s.update_staging_size);
    r.Read(s.update_last_wire_status);
    r.Read(s.update_final_seen);
    r.ReadBytes(s.update_target.data(), s.update_target.size());
    r.ReadBytes(s.update_staging.data(), s.update_staging.size());
}

} // namespace

void KtpMobileFModuleDevice::SaveState(StateWriter& w) {
    auto state = std::make_unique<ktp_mobile::State>();
    model_.CaptureState(*state);
    WriteModelState(w, *state);
    w.WriteBytes(dma_tx_.data(), dma_tx_.size());
    w.WriteBytes(dma_rx_.data(), dma_rx_.size());
    w.Write(dma_tx_bytes_);
    w.Write(dma_rx_buffer_pa_);
    w.Write(dma_rx_bytes_);
    const uint8_t flags[] = {
        static_cast<uint8_t>(dma_tx_ready_),
        static_cast<uint8_t>(dma_rx_pending_),
        static_cast<uint8_t>(dma_response_ready_),
        static_cast<uint8_t>(serial_complete_),
        static_cast<uint8_t>(model_cs_active_),
        static_cast<uint8_t>(panel_ack_high_),
        static_cast<uint8_t>(reset_held_),
        static_cast<uint8_t>(reset_low_seen_),
        static_cast<uint8_t>(adapter_error_),
        static_cast<uint8_t>(cyclic_ready_suppressed_),
    };
    w.WriteBytes(flags, sizeof(flags));
    w.Write(cyclic_ready_deadline_ns_);
}

void KtpMobileFModuleDevice::RestoreState(StateReader& r) {
    auto state = std::make_unique<ktp_mobile::State>();
    ReadModelState(r, *state);
    r.ReadBytes(dma_tx_.data(), dma_tx_.size());
    r.ReadBytes(dma_rx_.data(), dma_rx_.size());
    uint32_t dma_tx_bytes = 0;
    uint32_t dma_rx_buffer_pa = 0;
    uint32_t dma_rx_bytes = 0;
    r.Read(dma_tx_bytes);
    r.Read(dma_rx_buffer_pa);
    r.Read(dma_rx_bytes);
    uint8_t flags[10]{};
    r.ReadBytes(flags, sizeof(flags));
    int64_t cyclic_ready_deadline_ns = VirtualTimerList::kNoDeadline;
    r.Read(cyclic_ready_deadline_ns);

    if (!r.Ok())
        emu_.Get<Fatal>().Die("KTP Mobile F-module: truncated saved state");
    for (uint8_t flag : flags) {
        if (flag > 1u)
            emu_.Get<Fatal>().Die("KTP Mobile F-module: invalid saved adapter flags");
    }

    const bool dma_tx_ready = flags[0] != 0u;
    const bool dma_rx_pending = flags[1] != 0u;
    const bool dma_response_ready = flags[2] != 0u;
    const bool serial_complete = flags[3] != 0u;
    const bool model_cs_active = flags[4] != 0u;
    const bool reset_held = flags[6] != 0u;
    const bool reset_low_seen = flags[7] != 0u;
    const bool cyclic_ready_suppressed = flags[9] != 0u;
    const bool lengths_valid =
        dma_tx_bytes <= kTransferBytes && (dma_tx_bytes & 3u) == 0u &&
        dma_rx_bytes <= kTransferBytes && (dma_rx_bytes & 3u) == 0u;
    const bool receive_address_valid =
        dma_rx_bytes == 0u ||
        dma_rx_buffer_pa <= UINT32_MAX - (dma_rx_bytes - 1u);
    const bool transaction_valid =
        (!dma_tx_ready || dma_tx_bytes == kTransferBytes) &&
        (!dma_rx_pending || dma_rx_bytes == kTransferBytes) &&
        (!dma_response_ready || !dma_tx_ready) &&
        serial_complete == model_cs_active &&
        model_cs_active == (state->chip_select_asserted != 0u) &&
        (!reset_low_seen || reset_held) && receive_address_valid;
    const bool timer_valid =
        cyclic_ready_suppressed
            ? cyclic_ready_deadline_ns != VirtualTimerList::kNoDeadline
            : cyclic_ready_deadline_ns == VirtualTimerList::kNoDeadline;
    if (!lengths_valid || !transaction_valid || !timer_valid) {
        emu_.Get<Fatal>().Die("KTP Mobile F-module: invalid saved adapter state");
    }
    if (!selected_container_valid_ || state->approved_container_valid == 0u ||
        state->approved_container_sha256 != selected_container_sha256_) {
        emu_.Get<Fatal>().Die(
            "KTP Mobile F-module: saved state does not match selected FWF firmware");
    }
    const auto status = model_.RestoreState(*state);
    if (status != ktp_mobile::Status::Ok)
        emu_.Get<Fatal>().Die("KTP Mobile F-module: invalid saved model state (%u)",
                              static_cast<unsigned>(status));

    dma_tx_bytes_ = dma_tx_bytes;
    dma_rx_buffer_pa_ = dma_rx_buffer_pa;
    dma_rx_bytes_ = dma_rx_bytes;
    dma_tx_ready_ = dma_tx_ready;
    dma_rx_pending_ = dma_rx_pending;
    dma_response_ready_ = dma_response_ready;
    serial_complete_ = serial_complete;
    model_cs_active_ = model_cs_active;
    panel_ack_high_ = flags[5] != 0u;
    reset_held_ = reset_held;
    reset_low_seen_ = reset_low_seen;
    adapter_error_ = flags[8] != 0u;
    cyclic_ready_suppressed_ = cyclic_ready_suppressed;
    cyclic_ready_deadline_ns_ = cyclic_ready_deadline_ns;
}

void KtpMobileFModuleDevice::PostRestore() {
    FlushDmaReceive();
    cyclic_ready_timer_->Arm(cyclic_ready_suppressed_
                                 ? cyclic_ready_deadline_ns_
                                 : VirtualTimerList::kNoDeadline);
    if (ready_changed_) ready_changed_(ready_changed_context_);
}

REGISTER_SERVICE_AS(KtpMobileFModuleDevice, Imx6EcspiEndpoint);
