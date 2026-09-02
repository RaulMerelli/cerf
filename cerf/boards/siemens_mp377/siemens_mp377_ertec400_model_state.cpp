#include "siemens_mp377_ertec400_model.h"

#include "../../state/state_stream.h"

void SiemensMp377Ertec400Model::SaveState(StateWriter& writer) const {
    writer.Write(static_cast<uint64_t>(words_.size()));
    for (const auto& entry : words_) {
        writer.Write(entry.first);
        writer.Write(entry.second);
    }
    writer.Write(mdio_control_);
    writer.Write(last_mdio_register_);
    for (uint32_t index = 0; index < 32u; ++index) {
        writer.Write(mdio_phy_regs_[index]);
        writer.Write(static_cast<uint32_t>(mdio_phy_written_[index] != 0u));
    }
    writer.Write(switch_control_);
    writer.Write(switch_status_);
    writer.Write(access_sequence_);
    writer.Write(primary_readback_);
    writer.Write(secondary_readback_);
    writer.Write(primary_readback_sequence_);
    writer.Write(secondary_readback_sequence_);
    writer.Write(static_cast<uint32_t>(primary_readback_pending_));
    writer.Write(static_cast<uint32_t>(secondary_readback_pending_));
    writer.Write(irt_start_time_);
    writer.Write(switch_port_control_);
    writer.Write(cyclic_counter_low_);
    writer.Write(cyclic_counter_high_);
    writer.Write(cyclic_counter_increment_);
    writer.Write(cyclic_control_);
    writer.Write(multicast_port_mask_);
}

void SiemensMp377Ertec400Model::RestoreState(StateReader& reader) {
    uint64_t count = 0;
    reader.Read(count);
    words_.clear();
    for (uint64_t index = 0; index < count; ++index) {
        uint32_t key = 0;
        uint32_t value = 0;
        reader.Read(key);
        reader.Read(value);
        words_[key] = value;
    }
    reader.Read(mdio_control_);
    reader.Read(last_mdio_register_);
    for (uint32_t index = 0; index < 32u; ++index) {
        uint32_t written = 0;
        reader.Read(mdio_phy_regs_[index]);
        reader.Read(written);
        mdio_phy_written_[index] = static_cast<uint8_t>(written != 0u);
    }
    reader.Read(switch_control_);
    reader.Read(switch_status_);
    reader.Read(access_sequence_);
    reader.Read(primary_readback_);
    reader.Read(secondary_readback_);
    reader.Read(primary_readback_sequence_);
    reader.Read(secondary_readback_sequence_);
    uint32_t primary_pending = 0;
    uint32_t secondary_pending = 0;
    reader.Read(primary_pending);
    reader.Read(secondary_pending);
    primary_readback_pending_ = primary_pending != 0u;
    secondary_readback_pending_ = secondary_pending != 0u;
    reader.Read(irt_start_time_);
    reader.Read(switch_port_control_);
    reader.Read(cyclic_counter_low_);
    reader.Read(cyclic_counter_high_);
    reader.Read(cyclic_counter_increment_);
    reader.Read(cyclic_control_);
    reader.Read(multicast_port_mask_);
}
