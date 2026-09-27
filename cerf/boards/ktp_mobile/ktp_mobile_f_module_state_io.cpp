#include "ktp_mobile_f_module_state_io.h"

#include "ktp_mobile_f_module_model.h"
#include "../../state/state_stream.h"

#include <type_traits>

namespace ktp_mobile_f_module_state_io {

template <typename E>
void WriteEnum(StateWriter& w, const char* name, E value) {
    w.Write(name, static_cast<std::underlying_type_t<E>>(value));
}

template <typename E>
void ReadEnum(StateReader& r, const char* name, E& value) {
    std::underlying_type_t<E> raw{};
    r.Read(name, raw);
    value = static_cast<E>(raw);
}

void Write(StateWriter& w, const ktp_mobile::State& s) {
    WriteEnum(w, "last_reset", s.last_reset);
    WriteEnum(w, "module_phase", s.module_phase);
    WriteEnum(w, "update_phase", s.update_phase);
    w.Write("gpio5_ready", s.gpio5_ready);
    w.Write("gpio6_ack", s.gpio6_ack);
    w.Write("chip_select_asserted", s.chip_select_asserted);
    w.Write("startup_exchange_pending", s.startup_exchange_pending);
    w.Write("spi_bytes_transferred", s.spi_bytes_transferred);
    w.Write("reserved_spi", s.reserved_spi);
    w.WriteBytes("spi_rx", s.spi_rx.data(), s.spi_rx.size());
    w.WriteBytes("spi_tx", s.spi_tx.data(), s.spi_tx.size());
    w.WriteBytes("panel_cyclic_bytes", s.panel_cyclic_bytes.data(), s.panel_cyclic_bytes.size());
    w.WriteBytes("module_cyclic_bytes", s.module_cyclic_bytes.data(), s.module_cyclic_bytes.size());
    w.Write("panel_status_byte", s.panel_status_byte);
    w.Write("module_status_byte", s.module_status_byte);
    w.Write("startup_control_acknowledged", s.startup_control_acknowledged);
    w.Write("reserved_outer", s.reserved_outer);
    w.Write("next_module_relay_sequence", s.next_module_relay_sequence);
    w.Write("last_panel_relay_sequence", s.last_panel_relay_sequence);
    w.Write("active_module_relay_sequence", s.active_module_relay_sequence);
    w.Write("active_module_relay_length", s.active_module_relay_length);
    w.WriteBytes("active_module_relay", s.active_module_relay.data(), s.active_module_relay.size());
    w.Write("staged_module_record_bytes", s.staged_module_record_bytes);
    w.Write("reserved_relay", s.reserved_relay);
    w.WriteBytes("staged_module_records", s.staged_module_records.data(), s.staged_module_records.size());
    w.Write("valid", s.firmware.valid);
    w.Write("flash_materialized", s.firmware.flash_materialized);
    w.Write("reserved", s.firmware.reserved);
    w.Write("container_size", s.firmware.container_size);
    w.Write("payload_size", s.firmware.payload_size);
    w.WriteBytes("version", s.firmware.version.data(), s.firmware.version.size());
    w.WriteBytes("application_flash", s.application_flash.data(), s.application_flash.size());
    w.Write("approved_container_valid", s.approved_container_valid);
    w.WriteBytes("reserved_approved", s.reserved_approved.data(), s.reserved_approved.size());
    w.WriteBytes("approved_container_sha256", s.approved_container_sha256.data(), s.approved_container_sha256.size());
    w.Write("update_expected_sequence", s.update_expected_sequence);
    w.Write("update_staging_size", s.update_staging_size);
    w.Write("update_last_wire_status", s.update_last_wire_status);
    w.Write("update_final_seen", s.update_final_seen);
    w.WriteBytes("update_target", s.update_target.data(), s.update_target.size());
    w.WriteBytes("update_staging", s.update_staging.data(), s.update_staging.size());
}

void Read(StateReader& r, ktp_mobile::State& s) {
    ReadEnum(r, "last_reset", s.last_reset);
    ReadEnum(r, "module_phase", s.module_phase);
    ReadEnum(r, "update_phase", s.update_phase);
    r.Read("gpio5_ready", s.gpio5_ready);
    r.Read("gpio6_ack", s.gpio6_ack);
    r.Read("chip_select_asserted", s.chip_select_asserted);
    r.Read("startup_exchange_pending", s.startup_exchange_pending);
    r.Read("spi_bytes_transferred", s.spi_bytes_transferred);
    r.Read("reserved_spi", s.reserved_spi);
    r.ReadBytes("spi_rx", s.spi_rx.data(), s.spi_rx.size());
    r.ReadBytes("spi_tx", s.spi_tx.data(), s.spi_tx.size());
    r.ReadBytes("panel_cyclic_bytes", s.panel_cyclic_bytes.data(), s.panel_cyclic_bytes.size());
    r.ReadBytes("module_cyclic_bytes", s.module_cyclic_bytes.data(), s.module_cyclic_bytes.size());
    r.Read("panel_status_byte", s.panel_status_byte);
    r.Read("module_status_byte", s.module_status_byte);
    r.Read("startup_control_acknowledged", s.startup_control_acknowledged);
    r.Read("reserved_outer", s.reserved_outer);
    r.Read("next_module_relay_sequence", s.next_module_relay_sequence);
    r.Read("last_panel_relay_sequence", s.last_panel_relay_sequence);
    r.Read("active_module_relay_sequence", s.active_module_relay_sequence);
    r.Read("active_module_relay_length", s.active_module_relay_length);
    r.ReadBytes("active_module_relay", s.active_module_relay.data(), s.active_module_relay.size());
    r.Read("staged_module_record_bytes", s.staged_module_record_bytes);
    r.Read("reserved_relay", s.reserved_relay);
    r.ReadBytes("staged_module_records", s.staged_module_records.data(), s.staged_module_records.size());
    r.Read("valid", s.firmware.valid);
    r.Read("flash_materialized", s.firmware.flash_materialized);
    r.Read("reserved", s.firmware.reserved);
    r.Read("container_size", s.firmware.container_size);
    r.Read("payload_size", s.firmware.payload_size);
    r.ReadBytes("version", s.firmware.version.data(), s.firmware.version.size());
    r.ReadBytes("application_flash", s.application_flash.data(), s.application_flash.size());
    r.Read("approved_container_valid", s.approved_container_valid);
    r.ReadBytes("reserved_approved", s.reserved_approved.data(), s.reserved_approved.size());
    r.ReadBytes("approved_container_sha256", s.approved_container_sha256.data(), s.approved_container_sha256.size());
    r.Read("update_expected_sequence", s.update_expected_sequence);
    r.Read("update_staging_size", s.update_staging_size);
    r.Read("update_last_wire_status", s.update_last_wire_status);
    r.Read("update_final_seen", s.update_final_seen);
    r.ReadBytes("update_target", s.update_target.data(), s.update_target.size());
    r.ReadBytes("update_staging", s.update_staging.data(), s.update_staging.size());
}

}
