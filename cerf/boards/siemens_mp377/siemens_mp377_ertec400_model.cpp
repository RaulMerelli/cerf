#include "siemens_mp377_ertec400_model.h"

#include "siemens_mp377_ertec400.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"

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
    return access_sequence_ > write_sequence && access_sequence_ - write_sequence <= 8u;
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

uint32_t SiemensMp377Ertec400Model::MdioDefault(uint32_t reg) const {
    /* Broadcom BCM5221 Preliminary Data Sheet 5221-DS07-R, Tables 7 and 30. */
    switch (reg & 0x1Fu) {
    case 0x00u: return 0x00003100u;
    case 0x01u: return 0x0000786Du;
    case 0x02u: return 0x00000040u;
    case 0x03u: return 0x000061E0u;
    case 0x04u: return 0x000001E1u;
    case 0x05u: return 0x000001E1u;
    case 0x1Fu: return 0x0000000Bu;
    default: emu_.Get<Fatal>().Die("[ERTEC400] MDIO read of unmodelled PHY register %u", reg & 0x1Fu);
    }
}

uint32_t SiemensMp377Ertec400Model::ReadMdio(uint32_t reg) const {
    const uint32_t index = reg & 0x1Fu;
    switch (index) {
    case 0x00u:
    case 0x04u:
    case 0x1Fu: return mdio_phy_written_[index] ? mdio_phy_regs_[index] : MdioDefault(index);
    case 0x01u:
    case 0x02u:
    case 0x03u:
    case 0x05u: return MdioDefault(index);
    default: return MdioDefault(index);
    }
}

void SiemensMp377Ertec400Model::WriteMdio(uint32_t reg, uint32_t value) {
    const uint32_t index = reg & 0x1Fu;
    switch (index) {
    case 0x01u:
    case 0x02u:
    case 0x03u:
    case 0x05u: return;
    case 0x00u: value &= ~0x00008000u; break;
    case 0x04u: break;
    case 0x1Fu: {
        const uint32_t old_value = mdio_phy_written_[index] ? mdio_phy_regs_[index] : MdioDefault(index);
        value = (old_value & ~0x0080u) | (value & 0x0080u);
        break;
    }
    default: break;
    }
    mdio_phy_regs_[index] = value & 0x0000FFFFu;
    mdio_phy_written_[index] = 1u;
}

uint32_t SiemensMp377Ertec400Model::FreeRunningCounter10ns() const {
    return static_cast<uint32_t>(access_sequence_ * 100000ull);
}

uint32_t SiemensMp377Ertec400Model::IrtStartTime() const {
    return irt_start_time_;
}

uint32_t SiemensMp377Ertec400Model::SwitchPortControl() const {
    return switch_port_control_;
}

uint32_t SiemensMp377Ertec400Model::CyclicCounterLow() const {
    return cyclic_counter_low_;
}

uint32_t SiemensMp377Ertec400Model::CyclicCounterHigh() const {
    return cyclic_counter_high_;
}

uint32_t SiemensMp377Ertec400Model::CyclicCounterIncrement() const {
    return cyclic_counter_increment_;
}

uint32_t SiemensMp377Ertec400Model::CyclicControl() const {
    return cyclic_control_;
}

uint32_t SiemensMp377Ertec400Model::MulticastPortMask() const {
    return multicast_port_mask_;
}

bool SiemensMp377Ertec400Model::IsNrtDmacCommand(uint32_t offset) {
    using namespace siemens_mp377;
    if (offset < kErtecNrtDmacBaseOffset) return false;
    const uint32_t relative = offset - kErtecNrtDmacBaseOffset;
    return relative % kErtecNrtDmacStride == 0u && relative / kErtecNrtDmacStride < kErtecNrtDmacPortCount;
}

bool SiemensMp377Ertec400Model::IsNrtDmacAddress(uint32_t offset) {
    using namespace siemens_mp377;
    if (offset < kErtecNrtDmacBaseOffset) return false;
    const uint32_t relative = offset - kErtecNrtDmacBaseOffset;
    const uint32_t slot = relative / kErtecNrtDmacStride;
    const uint32_t within = relative % kErtecNrtDmacStride;
    return slot < kErtecNrtDmacPortCount && (within == 4u || within == 8u);
}

bool SiemensMp377Ertec400Model::IsReadableRegister(uint32_t offset) {
    using namespace siemens_mp377;
    if (IsNrtDmacCommand(offset) || IsNrtDmacAddress(offset)) return true;
    switch (offset) {
    case kErtecResetControlOffset:
    case 0x00015000u:
    case 0x00015004u:
    case 0x0001102Cu:
    case 0x00011400u:
    case 0x00011414u:
    case 0x00011418u:
    case 0x00019010u:
    case 0x00016024u:
    case 0x0001500Cu:
    case 0x00015014u:
    case 0x0001501Cu:
    case kErtecSerPrimCommandOffset:
    case kErtecSerSecCommandOffset:
    case kErtecSerConfCommandOffset:
    case kErtecIrtControlOffset:
    case kErtecIrtControlOffset + 0x04u:
    case kErtecIrqStatusLoOffset:
    case kErtecIrqStatusHiOffset:
    case kErtecIrtStartOffset:
    case 0x0001640Cu:
    case kErtecSwiControlOffset:
    case kErtecSwiStatusOffset:
    case kErtecEddHwTypeOffset: return true;
    default: return false;
    }
}

bool SiemensMp377Ertec400Model::IsWritableRegister(uint32_t offset) {
    using namespace siemens_mp377;
    if (IsNrtDmacCommand(offset) || IsNrtDmacAddress(offset)) return true;
    if (IsWriteOnlyRegister(offset)) return true;
    switch (offset) {
    case kErtecResetControlOffset:
    case kErtecBootReadyOffset:
    case kErtecSerPrimCommandOffset:
    case kErtecSerSecCommandOffset:
    case kErtecSerConfCommandOffset:
    case kErtecFlowControlOffset - 4u:
    case kErtecFlowControlOffset:
    case kErtecIrtControlOffset:
    case kErtecIrtControlOffset + 0x04u:
    case 0x00013048u:
    case 0x00013404u:
    case 0x0001102Cu:
    case 0x00011400u:
    case 0x00011418u:
    case 0x00019010u:
    case 0x00016024u:
    case 0x00015000u:
    case 0x00015004u:
    case 0x0001500Cu:
    case 0x00015014u:
    case 0x0001501Cu:
    case kErtecIrqStatusLoOffset:
    case kErtecIrqStatusHiOffset:
    case kErtecIrqAckOffset:
    case kErtecIrtStartOffset:
    case kErtecIrtStartOffset + 4u:
    case kErtecSwiControlOffset:
    case kErtecSwiStatusOffset:
    case kErtecEddHwTypeOffset:
    case 0x00011414u: return true;
    default: return false;
    }
}

bool SiemensMp377Ertec400Model::IsWriteOnlyRegister(uint32_t offset) {
    switch (offset) {
    case 0x00011000u:
    case 0x00011004u:
    case 0x00011008u:
    case 0x0001100Cu:
    case 0x00011010u:
    case 0x00011014u:
    case 0x00011018u:
    case 0x0001101Cu:
    case 0x00011020u:
    case 0x00011024u:
    case 0x00011028u:
    case 0x00011030u:
    case 0x00011034u:
    case 0x00011038u:
    case 0x00011404u:
    case 0x00011408u:
    case 0x00011410u:
    case 0x0001141Cu:
    case 0x00011424u:
    case 0x00012000u:
    case 0x00012004u:
    case 0x00016000u:
    case 0x00016004u:
    case 0x00016008u:
    case 0x0001600Cu:
    case 0x00016010u:
    case 0x00016014u:
    case 0x00016018u:
    case 0x0001601Cu:
    case 0x00016020u:
    case 0x00016028u:
    case 0x0001602Cu:
    case 0x00016030u:
    case 0x00016034u:
    case 0x00016038u:
    case 0x0001603Cu:
    case 0x00016040u:
    case 0x00016044u:
    case 0x00016410u:
    case 0x00016414u:
    case 0x00016418u:
    case 0x00016420u:
    case 0x00016424u:
    case 0x00016428u:
    case 0x00018404u:
    case 0x00019024u:
    case 0x00019028u:
    case 0x0001902Cu:
    case 0x0001903Cu:
    case 0x00019040u:
    case 0x00019044u:
    case 0x00019048u:
    case 0x00019050u:
    case 0x00019054u: return true;
    default: return false;
    }
}

uint32_t SiemensMp377Ertec400Model::DefaultRead(uint32_t offset) const {
    using namespace siemens_mp377;
    switch (offset) {
    /* ERTEC 400 Manual V1.2.2, RES_CTRL_REG (0x4000_260C). */
    case kErtecResetControlOffset: return 0x00000100u;
    case kErtecEddHwTypeOffset: return kErtecEddHwTypeErtec400Rev5;
    case kErtecSwiControlOffset: return switch_control_;
    case kErtecSwiStatusOffset: return switch_status_;
    case kErtecBootReadyOffset: return kErtecBootReadyBit;
    case 0x00011414u: return FreeRunningCounter10ns();
    case 0x00015000u: return ReadMdio(last_mdio_register_);
    case 0x00015004u: return mdio_control_ & ~0x00000800u;
    case 0x0001500Cu: return 0x00000003u;
    case 0x00015014u:
    case 0x0001501Cu: return 0x00000024u;
    case kErtecIrqStatusLoOffset:
    case kErtecIrtControlOffset:
    case kErtecIrtControlOffset + 0x04u:
    case kErtecSerPrimCommandOffset: return 0u;
    case kErtecSerConfCommandOffset: return kErtecSerCommandOkBit;
    }
    if (IsNrtDmacCommand(offset)) return 0x00000004u;
    emu_.Get<Fatal>().Die("ERTEC400 read without modelled reset/readback value at offset 0x%08X", offset);
}

void SiemensMp377Ertec400Model::PublishSwitchReady(uint32_t status) {
    switch_status_ = status;
    SetWord(siemens_mp377::kErtecSwiStatusOffset, status);
}

void SiemensMp377Ertec400Model::Reset() {
    using namespace siemens_mp377;
    words_.clear();
    mdio_control_ = 0u;
    last_mdio_register_ = 0x02u;
    mdio_phy_regs_.fill(0u);
    mdio_phy_written_.fill(0u);
    switch_control_ = 0u;
    switch_status_ = kErtecSwiStatusAllDone;
    access_sequence_ = 0u;
    primary_readback_ = 0u;
    secondary_readback_ = 0u;
    primary_readback_sequence_ = 0u;
    secondary_readback_sequence_ = 0u;
    primary_readback_pending_ = false;
    secondary_readback_pending_ = false;
    irt_start_time_ = 0u;
    switch_port_control_ = 0u;
    cyclic_counter_low_ = 0u;
    cyclic_counter_high_ = 0u;
    cyclic_counter_increment_ = 0u;
    cyclic_control_ = 0u;
    multicast_port_mask_ = 0u;
    SetWord(kErtecResetControlOffset, 0x00000100u);
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
    SetWord(siemens_mp377::kErtecSerConfCommandOffset, siemens_mp377::kErtecSerCommandOkBit);
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

void SiemensMp377Ertec400Model::WriteIrtStartTime(uint32_t value) {
    irt_start_time_ = value;
}

void SiemensMp377Ertec400Model::WriteIrtStartCommand(uint32_t value) {
    /* siemens_mp377_v1040 eddertec400.dll sub_28F3F80 (EDDDeviceSetupSER)
       and sub_290BCA4 (SERReset). */
    if (value == 0x0000000Cu) return;
    if (value != 0x00000100u) {
        emu_.Get<Fatal>().Die("ERTEC400 unsupported IRT start command 0x%08X", value);
    }
    irt_start_time_ = 0u;
}

void SiemensMp377Ertec400Model::WriteSwitchPortControl(uint32_t value) {
    /* siemens_mp377_v1040 eddertec400.dll sub_2912FA8 (SwiMiscSetDisable). */
    switch_port_control_ = value;
}

void SiemensMp377Ertec400Model::WriteCyclicCounterLow(uint32_t value) {
    /* siemens_mp377_v1040 eddertec400.dll sub_28ED7B4 (EDDCyc_UpdateSoftwareCounter). */
    cyclic_counter_low_ = value;
}

void SiemensMp377Ertec400Model::WriteCyclicCounterHigh(uint32_t value) {
    /* siemens_mp377_v1040 eddertec400.dll sub_28ED7B4 (EDDCyc_UpdateSoftwareCounter). */
    cyclic_counter_high_ = value;
}

void SiemensMp377Ertec400Model::WriteCyclicCounterIncrement(uint32_t value) {
    /* siemens_mp377_v1040 eddertec400.dll sub_28ED18C (EDDCyc_SetSoftwareCounter). */
    cyclic_counter_increment_ = value;
}

void SiemensMp377Ertec400Model::WriteCyclicControl(uint32_t value) {
    /* siemens_mp377_v1040 eddertec400.dll sub_28EEE9C (EDDCyc_Init). */
    cyclic_control_ = value;
}

void SiemensMp377Ertec400Model::WriteMulticastPortMask(uint32_t value) {
    /* siemens_mp377_v1040 eddertec400.dll sub_28F1BF8 (EDDGenEnableMC). */
    multicast_port_mask_ = value;
}
