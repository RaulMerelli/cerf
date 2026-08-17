#include "siemens_mp377_ertec400_model.h"

#include "siemens_mp377_ertec400.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../state/state_stream.h"

REGISTER_SERVICE(SiemensMp377Ertec400Model);

bool SiemensMp377Ertec400Model::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && board->GetBoard() == Board::SiemensMP377;
}

void SiemensMp377Ertec400Model::BeginAccess() {
    ++access_sequence_;
}

uint32_t SiemensMp377Ertec400Model::CanonicalKey(uint32_t offset) {
    return siemens_mp377::kErtecBar3Base + offset;
}

bool SiemensMp377Ertec400Model::GetWord(uint32_t offset, uint32_t& value) const {
    const auto it = words_.find(CanonicalKey(offset));
    if (it == words_.end()) return false;
    value = it->second;
    return true;
}

void SiemensMp377Ertec400Model::SetWord(uint32_t offset, uint32_t value) {
    words_[CanonicalKey(offset)] = value;
}

bool SiemensMp377Ertec400Model::ExposeImmediateReadback(uint64_t write_sequence) const {
    return access_sequence_ > write_sequence &&
           access_sequence_ - write_sequence <= 8u;
}

bool SiemensMp377Ertec400Model::ConsumePrimaryReadback(uint32_t& value) {
    if (!primary_readback_pending_) return false;
    primary_readback_pending_ = false;
    const bool expose = ExposeImmediateReadback(primary_readback_sequence_);
    value = expose ? primary_readback_ : 0u;
    if (!expose) SetWord(siemens_mp377::kErtecSerPrimCommandOffset, 0u);
    return true;
}

bool SiemensMp377Ertec400Model::ConsumeSecondaryReadback(uint32_t& value) {
    if (!secondary_readback_pending_) return false;
    secondary_readback_pending_ = false;
    const bool expose = ExposeImmediateReadback(secondary_readback_sequence_);
    value = expose ? secondary_readback_ : 0u;
    if (!expose) SetWord(siemens_mp377::kErtecSerSecCommandOffset, 0u);
    return true;
}

uint32_t SiemensMp377Ertec400Model::MdioDefault(uint32_t reg) {
    switch (reg & 0x1Fu) {
        case 0x00u: return 0x00003100u;
        case 0x01u: return 0x0000786Du;
        case 0x02u: return 0x00000040u;
        case 0x03u: return 0x000061E0u;
        case 0x04u: return 0x000001E1u;
        case 0x05u: return 0x000001E1u;
        default: return 0u;
    }
}

uint32_t SiemensMp377Ertec400Model::ReadMdio(uint32_t reg) const {
    const uint32_t index = reg & 0x1Fu;
    switch (index) {
        case 0x01u:
        case 0x02u:
        case 0x03u:
        case 0x05u:
            return MdioDefault(index);
        default:
            return mdio_phy_written_[index] ? mdio_phy_regs_[index] : MdioDefault(index);
    }
}

void SiemensMp377Ertec400Model::WriteMdio(uint32_t reg, uint32_t value) {
    const uint32_t index = reg & 0x1Fu;
    switch (index) {
        case 0x01u:
        case 0x02u:
        case 0x03u:
        case 0x05u:
            return;
        case 0x00u:
            value &= ~0x00008000u;
            break;
        default:
            break;
    }
    mdio_phy_regs_[index] = value & 0x0000FFFFu;
    mdio_phy_written_[index] = 1u;
}

uint32_t SiemensMp377Ertec400Model::FreeRunningCounter10ns() const {
    return static_cast<uint32_t>(access_sequence_ * 100000ull);
}

bool SiemensMp377Ertec400Model::IsNrtDmacCommand(uint32_t offset) {
    using namespace siemens_mp377;
    if (offset < kErtecNrtDmacBaseOffset) return false;
    const uint32_t relative = offset - kErtecNrtDmacBaseOffset;
    return relative % kErtecNrtDmacStride == 0u &&
           relative / kErtecNrtDmacStride < kErtecNrtDmacPortCount;
}

uint32_t SiemensMp377Ertec400Model::DefaultRead(uint32_t offset) const {
    using namespace siemens_mp377;
    switch (offset) {
        case kErtecEddHwTypeOffset: return kErtecEddHwTypeErtec400Rev5;
        case kErtecSwiControlOffset: return switch_control_;
        case kErtecSwiStatusOffset: return switch_status_;
        case kErtecBootReadyOffset: return kErtecBootReadyBit;
        case 0x00011400u:
        case 0x00011404u:
        case 0x00011408u:
        case 0x00011410u:
        case 0x00011418u:
        case 0x0001141Cu:
        case 0x00011424u:
            return 0u;
        case 0x00011414u: return FreeRunningCounter10ns();
        case 0x00015000u: return ReadMdio(last_mdio_register_);
        case 0x00015004u: return mdio_control_ & ~0x00000800u;
        case 0x0001500Cu: return 0x00000003u;
        case 0x00015014u:
        case 0x0001501Cu:
            return 0x00000024u;
        case kErtecIrqStatusLoOffset:
        case kErtecIrqAckOffset:
        case kErtecIrtControlOffset:
        case kErtecIrtControlOffset + 0x04u:
        case 0x00013048u:
        case 0x00013404u:
        case kErtecIrtStartOffset:
        case kErtecIrtStartOffset + 4u:
        case kErtecIrtTimerBaseOffset + 0x00u:
        case kErtecIrtTimerBaseOffset + 0x04u:
        case kErtecIrtTimerBaseOffset + 0x08u:
        case kErtecIrtTimerBaseOffset + 0x0Cu:
        case kErtecIrtTimerBaseOffset + 0x10u:
        case kErtecIrtTimerBaseOffset + 0x14u:
        case kErtecIrtTimerBaseOffset + 0x18u:
        case kErtecIrtTimerBaseOffset + 0x1Cu:
        case kErtecSerPrimCommandOffset:
        case kErtecSerSecCommandOffset:
        case kErtecFlowControlOffset - 4u:
        case kErtecFlowControlOffset:
            return 0u;
        case kErtecSerConfCommandOffset:
            return kErtecSerCommandOkBit;
        default:
            return IsNrtDmacCommand(offset) ? 0x00000004u : 0u;
    }
}

void SiemensMp377Ertec400Model::PublishSwitchReady(uint32_t status) {
    switch_status_ = status;
    SetWord(siemens_mp377::kErtecSwiStatusOffset, status);
}

void SiemensMp377Ertec400Model::PublishIdleInitState() {
    using namespace siemens_mp377;
    SetWord(kErtecBootReadyOffset, kErtecBootReadyBit);
    PublishSwitchReady(kErtecSwiStatusAllDone);
    SetWord(kErtecIrtControlOffset, 0u);
    SetWord(kErtecIrtControlOffset + 0x04u, 0u);
    SetWord(0x00013048u, 0u);
    SetWord(0x00013404u, 0u);
    SetWord(kErtecSerPrimCommandOffset, 0u);
    SetWord(kErtecSerSecCommandOffset, 0u);
    SetWord(kErtecSerConfCommandOffset, kErtecSerCommandOkBit);
    SetWord(kErtecFlowControlOffset - 4u, 0u);
    SetWord(kErtecFlowControlOffset, 0u);
    for (uint32_t index = 0; index < kErtecNrtDmacPortCount; ++index) {
        SetWord(kErtecNrtDmacBaseOffset + index * kErtecNrtDmacStride, 0x00000004u);
    }
}

void SiemensMp377Ertec400Model::ApplySwitchControl(uint32_t value) {
    using namespace siemens_mp377;
    switch_control_ = value;
    SetWord(kErtecSwiControlOffset, value);
    if ((value & 0x00000005u) == 0x00000005u) {
        PublishSwitchReady(kErtecSwiStatusMinMode);
    } else if (value == 0x00000002u) {
        PublishSwitchReady(kErtecSwiStatusAllDone);
    } else {
        uint32_t status = switch_status_;
        if ((value & 0x00000008u) == 0u) status |= 0x00000008u;
        if ((status & 0x0000F000u) != 0x0000F000u) status |= 0x0000F000u;
        PublishSwitchReady(status);
    }
}

void SiemensMp377Ertec400Model::CompletePrimaryCommand(uint32_t value) {
    primary_readback_ = value;
    primary_readback_sequence_ = access_sequence_;
    primary_readback_pending_ = true;
    SetWord(siemens_mp377::kErtecSerPrimCommandOffset, 0u);
    SetWord(siemens_mp377::kErtecSerConfCommandOffset,
            siemens_mp377::kErtecSerCommandOkBit);
}

void SiemensMp377Ertec400Model::CompleteSecondaryCommand(uint32_t value) {
    secondary_readback_ = value;
    secondary_readback_sequence_ = access_sequence_;
    secondary_readback_pending_ = true;
    SetWord(siemens_mp377::kErtecSerSecCommandOffset, 0u);
}

void SiemensMp377Ertec400Model::WriteMdioControl(uint32_t value) {
    const uint32_t phy_register = value & 0x1Fu;
    last_mdio_register_ = phy_register;
    mdio_control_ = value & ~0x00000800u;
    SetWord(0x00015004u, mdio_control_);
    if ((value & 0x00000400u) != 0u) {
        uint32_t data = 0;
        GetWord(0x00015000u, data);
        WriteMdio(phy_register, data);
    }
    SetWord(0x00015000u, ReadMdio(phy_register));
}

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
}

