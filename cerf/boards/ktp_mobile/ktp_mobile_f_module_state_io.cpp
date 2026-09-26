#include "ktp_mobile_f_module_state_io.h"

#include "ktp_mobile_f_module_model.h"
#include "../../state/state_stream.h"

#include <type_traits>

namespace ktp_mobile_f_module_state_io {

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

void Write(StateWriter& w, const ktp_mobile::State& s) {
    WriteEnum(w, s.last_reset);
    WriteEnum(w, s.module_phase);
    WriteEnum(w, s.update_phase);
    w.Write(s.gpio5_ready);
    w.Write(s.gpio6_ack);
    w.Write(s.chip_select_asserted);
    w.Write(s.startup_exchange_pending);
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

void Read(StateReader& r, ktp_mobile::State& s) {
    ReadEnum(r, s.last_reset);
    ReadEnum(r, s.module_phase);
    ReadEnum(r, s.update_phase);
    r.Read(s.gpio5_ready);
    r.Read(s.gpio6_ack);
    r.Read(s.chip_select_asserted);
    r.Read(s.startup_exchange_pending);
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

}
