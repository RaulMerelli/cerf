#include "siemens_mp377_sm501_ac97.h"

#include "siemens_mp377_sm501_internal.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"

namespace siemens_mp377 {

bool SiemensMp377Sm501Ac97::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && board->GetBoard() == Board::SiemensMP377;
}

void SiemensMp377Sm501Ac97::OnReady() {
    registers_ = &emu_.Get<SiemensMp377Sm501Regs>();
    ResetController();
}

bool SiemensMp377Sm501Ac97::IsRegister(uint32_t offset) {
    return IsTxRegister(offset) || IsRxRegister(offset) ||
           offset == kControlStatusReg;
}

bool SiemensMp377Sm501Ac97::IsTxRegister(uint32_t offset) {
    return offset == kTxSlot0TagReg || offset == kTxSlot1CmdAddrReg ||
           offset == kTxSlot2CmdDataReg || offset == kTxSlot3PcmLeftReg ||
           offset == kTxSlot4PcmRightReg;
}

bool SiemensMp377Sm501Ac97::IsRxRegister(uint32_t offset) {
    return offset == kRxSlot0TagReg || offset == kRxSlot1StatusAddrReg ||
           offset == kRxSlot2StatusDataReg ||
           offset == kRxSlot3PcmLeftReg || offset == kRxSlot4PcmRightReg;
}

void SiemensMp377Sm501Ac97::ResetController() {
    ResetCodec(true);
    control_ = 0u;
    tx_slot0_ = tx_slot1_ = tx_slot2_ = tx_slot3_ = tx_slot4_ = 0u;
    rx_slot0_ = rx_slot1_ = rx_slot2_ = rx_slot3_ = rx_slot4_ = 0u;
    drop_count_ = 0u;
    irq_pending_ = false;
    codec_ready_ = false;
    bclk_running_ = false;
    tx_dirty_mask_ = 0u;
    registers_->regs_[kTxSlot0TagReg / 4u] = 0u;
    registers_->regs_[kTxSlot1CmdAddrReg / 4u] = 0u;
    registers_->regs_[kTxSlot2CmdDataReg / 4u] = 0u;
    registers_->regs_[kTxSlot3PcmLeftReg / 4u] = 0u;
    registers_->regs_[kTxSlot4PcmRightReg / 4u] = 0u;
    registers_->regs_[kRxSlot0TagReg / 4u] = 0u;
    registers_->regs_[kRxSlot1StatusAddrReg / 4u] = 0u;
    registers_->regs_[kRxSlot2StatusDataReg / 4u] = 0u;
    registers_->regs_[kRxSlot3PcmLeftReg / 4u] = 0u;
    registers_->regs_[kRxSlot4PcmRightReg / 4u] = 0u;
    registers_->regs_[kControlStatusReg / 4u] = 0u;
}

uint32_t SiemensMp377Sm501Ac97::Read(uint32_t off, bool clear_irq) {
    UpdateStatus();
    uint32_t value = 0;
    switch (off) {
    case kTxSlot0TagReg: value = tx_slot0_; break;
    case kTxSlot1CmdAddrReg: value = tx_slot1_; break;
    case kTxSlot2CmdDataReg: value = tx_slot2_; break;
    case kTxSlot3PcmLeftReg: value = tx_slot3_; break;
    case kTxSlot4PcmRightReg: value = tx_slot4_; break;
    case kRxSlot0TagReg: value = rx_slot0_; break;
    case kRxSlot1StatusAddrReg: value = rx_slot1_; break;
    case kRxSlot2StatusDataReg: value = rx_slot2_; break;
    case kRxSlot3PcmLeftReg: value = rx_slot3_; break;
    case kRxSlot4PcmRightReg: value = rx_slot4_; break;
    case kControlStatusReg:
        value = ControlStatusValue();
        if (clear_irq) ClearInterrupt();
        break;
    default: value = registers_->regs_[off / 4u]; break;
    }
    registers_->regs_[off / 4u] = value;
    return value;
}

void SiemensMp377Sm501Ac97::Write(uint32_t off, uint32_t old_value, uint32_t value) {
    switch (off) {
    case kTxSlot0TagReg:
        tx_slot0_ = value & 0x0000F800u;
        registers_->regs_[off / 4u] = tx_slot0_;
        NoteTxSlotWrite(kDirtyTag);
        break;
    case kTxSlot1CmdAddrReg:
        tx_slot1_ = value & (kCmdReadBit | kCmdIndexMask);
        registers_->regs_[off / 4u] = tx_slot1_;
        NoteTxSlotWrite(kDirtySlot1);
        break;
    case kTxSlot2CmdDataReg:
        tx_slot2_ = value & kCmdDataMask;
        registers_->regs_[off / 4u] = tx_slot2_;
        NoteTxSlotWrite(kDirtySlot2);
        break;
    case kTxSlot3PcmLeftReg:
        tx_slot3_ = value & kSlotDataMask;
        registers_->regs_[off / 4u] = tx_slot3_;
        NoteTxSlotWrite(kDirtySlot3);
        break;
    case kTxSlot4PcmRightReg:
        tx_slot4_ = value & kSlotDataMask;
        registers_->regs_[off / 4u] = tx_slot4_;
        NoteTxSlotWrite(kDirtySlot4);
        break;
    case kRxSlot0TagReg:
    case kRxSlot1StatusAddrReg:
    case kRxSlot2StatusDataReg:
    case kRxSlot3PcmLeftReg:
    case kRxSlot4PcmRightReg:
        registers_->regs_[off / 4u] = old_value;
        break;
    case kControlStatusReg: {
        const uint32_t writable = kCtrlEnable | kCtrlColdReset | kCtrlWarmReset |
                                  kCtrlWakeIrqEnable | kCtrlStopSync;
        control_ = value & writable;
        if ((value & kCtrlColdReset) != 0u) {
            ResetCodec(true);
        } else if ((value & kCtrlWarmReset) != 0u) {
            codec_registers_[0x26u / 2u] &= static_cast<uint16_t>(~(1u << 12));
            codec_ready_ = true;
        }
        UpdateStatus();
        registers_->regs_[off / 4u] = ControlStatusValue();
        break;
    }
    default:
        registers_->regs_[off / 4u] = value;
        break;
    }
}

uint8_t SiemensMp377Sm501Ac97::ReadByte(uint32_t off, bool clear_irq) {
    const uint32_t word = Read(off & ~3u, clear_irq && ((off & ~3u) == kControlStatusReg));
    return static_cast<uint8_t>(word >> ((off & 3u) * 8u));
}

void SiemensMp377Sm501Ac97::WriteByte(uint32_t off, uint8_t value) {
    const uint32_t word_off = off & ~3u;
    const uint32_t byte = off & 3u;
    const uint32_t shift = byte * 8u;

    /* The 8051 side is byte-wide.  Do not treat every byte write as a complete
       AC97 slot update: slot words carry useful bits across bytes 1/2, and the
       firmware normally writes them byte-by-byte.  Commit a TX slot only when
       the last significant byte has been written. */
    auto patch_word = [&](uint32_t current) -> uint32_t {
        return (current & ~(0xFFu << shift)) | (static_cast<uint32_t>(value) << shift);
    };

    switch (word_off) {
    case kTxSlot0TagReg:
        tx_slot0_ = patch_word(tx_slot0_) & 0x0000F800u;
        registers_->regs_[word_off / 4u] = tx_slot0_;
        if (byte == 1u) NoteTxSlotWrite(kDirtyTag);
        return;
    case kTxSlot1CmdAddrReg:
        tx_slot1_ = patch_word(tx_slot1_) & (kCmdReadBit | kCmdIndexMask);
        registers_->regs_[word_off / 4u] = tx_slot1_;
        if (byte == 2u) NoteTxSlotWrite(kDirtySlot1);
        return;
    case kTxSlot2CmdDataReg:
        tx_slot2_ = patch_word(tx_slot2_) & kCmdDataMask;
        registers_->regs_[word_off / 4u] = tx_slot2_;
        if (byte == 2u) NoteTxSlotWrite(kDirtySlot2);
        return;
    case kTxSlot3PcmLeftReg:
        tx_slot3_ = patch_word(tx_slot3_) & kSlotDataMask;
        registers_->regs_[word_off / 4u] = tx_slot3_;
        if (byte == 2u) NoteTxSlotWrite(kDirtySlot3);
        return;
    case kTxSlot4PcmRightReg:
        tx_slot4_ = patch_word(tx_slot4_) & kSlotDataMask;
        registers_->regs_[word_off / 4u] = tx_slot4_;
        if (byte == 2u) NoteTxSlotWrite(kDirtySlot4);
        return;
    default: {
        uint32_t word = Read(word_off, false);
        word = patch_word(word);
        Write(word_off, registers_->regs_[word_off / 4u], word);
        return;
    }
    }
}

uint32_t SiemensMp377Sm501Ac97::ControlStatusValue() const {
    uint32_t status = control_ & (kCtrlEnable | kCtrlColdReset | kCtrlWarmReset |
                                             kCtrlWakeIrqEnable | kCtrlStopSync);
    status |= (drop_count_ & 0x3Fu) << 10;
    if (bclk_running_) status |= kCtrlBclkRunning;

    uint32_t st = 0;
    if ((control_ & kCtrlEnable) == 0u) st = 0;             // Off
    else if ((control_ & (kCtrlColdReset | kCtrlWarmReset)) != 0u) st = 1; // Reset
    else if (!codec_ready_ || !LinkPowered() ||
             (control_ & kCtrlStopSync) != 0u) st = 2; // Waiting
    else st = 3;                                                          // Active
    status |= st << kCtrlStatusShift;
    return status;
}

void SiemensMp377Sm501Ac97::UpdateStatus() {
    auto& power = emu_.Get<SiemensMp377Sm501PowerGpio>();
    const bool gate_on = power.IsGateEnabled(kSm501GateAc97I2sBit);
    const bool enabled = (control_ & kCtrlEnable) != 0u;
    const bool reset = (control_ & (kCtrlColdReset | kCtrlWarmReset)) != 0u;
    const bool sync_stopped = (control_ & kCtrlStopSync) != 0u;
    const bool link_powered = LinkPowered();
    const bool pins_routed = power.IsAc97LinkMuxed();
    const bool board_reset_released = power.IsCodecResetDeasserted();

    if (!board_reset_released || !link_powered) {
        codec_ready_ = false;
    } else if (enabled && !reset && gate_on && pins_routed && !sync_stopped) {
        codec_ready_ = true;
    }

    bclk_running_ = gate_on && enabled && !reset && !sync_stopped &&
                               link_powered && pins_routed && board_reset_released &&
                               codec_ready_;
    registers_->regs_[kRxSlot0TagReg / 4u] = rx_slot0_ =
        (codec_ready_ && link_powered) ? kTagValidFrame : 0u;
    registers_->regs_[kControlStatusReg / 4u] = ControlStatusValue();
}

void SiemensMp377Sm501Ac97::ResetCodec(bool cold) {
    for (uint16_t& r : codec_registers_) r = 0;

    /* CS4297A reset defaults, from the codec register table.  Register 00h is
       read-only; writes to it perform an AC'97 register reset. */
    codec_registers_[0x00u / 2u] = 0x1990u;
    codec_registers_[0x02u / 2u] = 0x8000u; /* Master Volume */
    codec_registers_[0x04u / 2u] = 0x8000u; /* Alternate Volume */
    codec_registers_[0x06u / 2u] = 0x8000u; /* Mono Volume */
    codec_registers_[0x0Au / 2u] = 0x0000u; /* PC Beep */
    codec_registers_[0x0Cu / 2u] = 0x8008u; /* Phone */
    codec_registers_[0x0Eu / 2u] = 0x8008u; /* Mic */
    codec_registers_[0x10u / 2u] = 0x8808u; /* Line In */
    codec_registers_[0x12u / 2u] = 0x8808u; /* CD */
    codec_registers_[0x14u / 2u] = 0x8808u; /* Video */
    codec_registers_[0x16u / 2u] = 0x8808u; /* Aux */
    codec_registers_[0x18u / 2u] = 0x8808u; /* PCM Out */
    codec_registers_[0x1Au / 2u] = 0x0000u; /* Record Select */
    codec_registers_[0x1Cu / 2u] = 0x8000u; /* Record Gain */
    codec_registers_[0x20u / 2u] = 0x0000u; /* General Purpose */
    codec_registers_[0x22u / 2u] = 0x0000u; /* 3D Control */
    codec_registers_[0x26u / 2u] = 0x0000u; /* PR controls; ready bits are derived */
    codec_registers_[0x28u / 2u] = 0x0200u; /* Primary codec, AMAP capable, VRA=0 */
    codec_registers_[0x2Cu / 2u] = 0xBB80u; /* 48 kHz DAC */
    codec_registers_[0x32u / 2u] = 0xBB80u; /* 48 kHz ADC */
    codec_registers_[0x5Eu / 2u] = 0x0080u; /* AC mode control */
    codec_registers_[0x60u / 2u] = 0x0023u; /* Misc crystal control */
    codec_registers_[0x68u / 2u] = 0x0000u; /* S/PDIF control */
    codec_registers_[0x7Cu / 2u] = 0x4352u;
    codec_registers_[0x7Eu / 2u] = 0x5913u;
    emu_.Get<SiemensMp377Sm501AudioOutput>().ResetFixedRate();
    codec_ready_ = cold ? false : codec_ready_;
    rx_slot1_ = 0u;
    rx_slot2_ = 0u;
    rx_slot3_ = 0u;
    rx_slot4_ = 0u;
}

void SiemensMp377Sm501Ac97::NoteTxSlotWrite(uint32_t dirty_bit) {
    tx_dirty_mask_ |= dirty_bit;
    ProcessFrame();
}

void SiemensMp377Sm501Ac97::ProcessFrame() {
    UpdateStatus();

    if ((tx_slot0_ & kTagValidFrame) == 0u) {
        rx_slot0_ = 0u;
        tx_dirty_mask_ &= kDirtyTag;
        registers_->regs_[kRxSlot0TagReg / 4u] = rx_slot0_;
        return;
    }

    const bool want_cmd = (tx_slot0_ & (kTagSlot1Valid | kTagSlot2Valid)) ==
                          (kTagSlot1Valid | kTagSlot2Valid);
    const bool have_cmd = (tx_dirty_mask_ & (kDirtySlot1 | kDirtySlot2)) ==
                          (kDirtySlot1 | kDirtySlot2);
    const bool want_pcm = (tx_slot0_ & (kTagSlot3Valid | kTagSlot4Valid)) ==
                          (kTagSlot3Valid | kTagSlot4Valid);
    const bool have_pcm = (tx_dirty_mask_ & (kDirtySlot3 | kDirtySlot4)) ==
                          (kDirtySlot3 | kDirtySlot4);

    if (!((want_cmd && have_cmd) || (want_pcm && have_pcm))) return;

    uint32_t rx_tag = codec_ready_ ? kTagValidFrame : 0u;
    uint32_t handled_dirty = 0u;
    if (!bclk_running_) {
        if (drop_count_ < 0x3Fu) ++drop_count_;
        tx_dirty_mask_ &= kDirtyTag;
        UpdateStatus();
        return;
    }

    if (want_cmd && have_cmd) {
        const uint32_t reg = (tx_slot1_ >> 12) & 0x7Eu;
        const bool is_read = (tx_slot1_ & kCmdReadBit) != 0u;
        uint16_t data = 0;
        if (is_read) {
            data = ReadCodecRegister(reg);
        } else {
            data = static_cast<uint16_t>((tx_slot2_ >> 4) & 0xFFFFu);
            WriteCodecRegister(reg, data);
        }
        rx_slot1_ = (reg & 0x7Eu) << 12;
        rx_slot2_ = static_cast<uint32_t>(data) << 4;
        rx_tag |= kTagSlot1Valid | kTagSlot2Valid;
        handled_dirty |= kDirtySlot1 | kDirtySlot2;
        RaiseInterrupt();
    }

    if (want_pcm && have_pcm) {
        emu_.Get<SiemensMp377Sm501AudioOutput>().QueueAc97PcmSample(
            tx_slot3_, tx_slot4_);
        /* RX Slot 3/4 validity describes ADC capture, not acknowledgement of
           playback data.  No capture engine is modeled here. */
        handled_dirty |= kDirtySlot3 | kDirtySlot4;
    }

    rx_slot0_ = rx_tag;
    registers_->regs_[kRxSlot0TagReg / 4u] = rx_slot0_;
    registers_->regs_[kRxSlot1StatusAddrReg / 4u] = rx_slot1_;
    registers_->regs_[kRxSlot2StatusDataReg / 4u] = rx_slot2_;
    registers_->regs_[kRxSlot3PcmLeftReg / 4u] = rx_slot3_;
    registers_->regs_[kRxSlot4PcmRightReg / 4u] = rx_slot4_;
    tx_dirty_mask_ &= ~handled_dirty;
    tx_dirty_mask_ &= ~kDirtyTag;
}
void SiemensMp377Sm501Ac97::RaiseInterrupt() {
    irq_pending_ = true;
    registers_->RaiseSm501InterruptBits(kSm501Ac97IrqBit);
}

void SiemensMp377Sm501Ac97::ClearInterrupt() {
    if (!irq_pending_ && (registers_->regs_[kSm501IrqStatusReg / 4u] & kSm501Ac97IrqBit) == 0u) return;
    irq_pending_ = false;
    registers_->ClearSm501InterruptBits(kSm501Ac97IrqBit);
}

uint16_t SiemensMp377Sm501Ac97::PowerStatusValue() const {
    const uint16_t control = static_cast<uint16_t>(codec_registers_[0x26u / 2u] & 0xFF00u);
    const bool pr0_adc = (control & (1u << 8)) != 0u;
    const bool pr1_dac = (control & (1u << 9)) != 0u;
    const bool pr2_anl = (control & (1u << 10)) != 0u;
    const bool pr3_ref = (control & (1u << 11)) != 0u;
    const bool pr4_link = (control & (1u << 12)) != 0u;

    uint16_t status = 0u;
    if (codec_ready_ && !pr3_ref && !pr4_link) status |= 0x0008u; /* REF */
    if ((status & 0x0008u) != 0u && !pr2_anl) status |= 0x0004u;             /* ANL */
    if ((status & 0x0004u) != 0u && !pr1_dac) status |= 0x0002u;             /* DAC */
    if ((status & 0x0004u) != 0u && !pr0_adc) status |= 0x0001u;             /* ADC */
    return static_cast<uint16_t>(control | status);
}

bool SiemensMp377Sm501Ac97::DacReady() const {
    return (PowerStatusValue() & 0x0002u) != 0u;
}

bool SiemensMp377Sm501Ac97::LinkPowered() const {
    return (codec_registers_[0x26u / 2u] & (1u << 12)) == 0u;
}

uint16_t SiemensMp377Sm501Ac97::ReadCodecRegister(uint32_t reg) const {
    reg &= 0x7Eu;
    const uint32_t idx = reg / 2u;
    if (idx >= 128u) return 0u;
    if (reg == 0x00u) return 0x1990u;
    if (reg == 0x26u) return PowerStatusValue();
    if (reg == 0x28u) return 0x0200u;
    if (reg == 0x2Cu || reg == 0x32u) return 0xBB80u;
    if (reg == 0x7Cu) return 0x4352u;
    if (reg == 0x7Eu) return 0x5913u;
    return codec_registers_[idx];
}

void SiemensMp377Sm501Ac97::WriteCodecRegister(uint32_t reg, uint16_t value) {
    reg &= 0x7Eu;
    const uint32_t idx = reg / 2u;
    if (idx >= 128u) return;

    if (reg == 0x00u) {
        ResetCodec(false);
        return;
    }

    switch (reg) {
    case 0x02u: case 0x04u:
        value &= 0xBF3Fu;
        break;
    case 0x06u:
        value &= 0x801Fu;
        break;
    case 0x0Au:
        value &= 0x801Eu;
        break;
    case 0x0Cu: case 0x0Eu:
        value &= 0x801Fu;
        break;
    case 0x10u: case 0x12u: case 0x14u: case 0x16u: case 0x18u:
        value &= 0x9F1Fu;
        break;
    case 0x1Au:
        value &= 0x0707u;
        break;
    case 0x1Cu:
        value &= 0x8F0Fu;
        break;
    case 0x20u:
        value &= 0x3F00u;
        break;
    case 0x22u:
        value &= 0x000Fu;
        break;
    case 0x26u:
        value &= 0xFF00u;
        break;
    case 0x28u: case 0x2Cu: case 0x32u: case 0x7Cu: case 0x7Eu:
        return;
    default:
        break;
    }

    codec_registers_[idx] = value;

    if (reg == 0x26u) {
        if (!LinkPowered()) {
            codec_ready_ = false;
            bclk_running_ = false;
        }
        if (!DacReady())
            emu_.Get<SiemensMp377Sm501AudioOutput>().HandleDacPowerDown();
        UpdateStatus();
    }
}


REGISTER_SERVICE(SiemensMp377Sm501Ac97);

}  // namespace siemens_mp377

