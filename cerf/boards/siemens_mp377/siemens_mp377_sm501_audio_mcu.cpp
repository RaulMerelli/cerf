#include "siemens_mp377_sm501_audio_mcu.h"
#include "siemens_mp377_sm501_internal.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../core/log.h"
#include "../../socs/irq_controller.h"
#include "../../state/state_stream.h"

namespace siemens_mp377 {
namespace {
constexpr int kMp377AudioIrqSource = 10;
} // namespace

bool SiemensMp377Sm501AudioMcu::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && board->GetBoard() == Board::SiemensMP377;
}

void SiemensMp377Sm501AudioMcu::OnReady() {
    auto& regs = emu_.Get<SiemensMp377Sm501Regs>();
    enabled_ = false;
    from_cpu_irq_pending_ = false;
    to_cpu_token_ = 0u;
    from_cpu_token_ = 0u;
    mailbox_busy_reads_ = 0u;
    regs.regs_[kResetReg / 4u] = 0u;
    regs.regs_[kModeReg / 4u] = 0u;
    regs.regs_[kToCpuIrqReg / 4u] = 0u;
    regs.regs_[kFromCpuIrqReg / 4u] = 0u;
    Reset();
}

bool SiemensMp377Sm501AudioMcu::IsControlRegister(uint32_t off) {
    return off >= kControlBase && off < kControlEnd;
}

bool SiemensMp377Sm501AudioMcu::IsSram(uint32_t off) {
    return off >= kProgramBase && off < kSramEnd;
}

bool SiemensMp377Sm501AudioMcu::IsProgramSram(uint32_t off) {
    return off >= kProgramBase && off < kProgramEnd;
}

bool SiemensMp377Sm501AudioMcu::IsPcmBuffer(uint32_t off) {
    return off >= kOutputBufferA && off < kOutputBufferB + kOutputBufferBytes;
}

bool SiemensMp377Sm501AudioMcu::IsEnabled() const {
    return enabled_;
}

uint32_t SiemensMp377Sm501AudioMcu::ReadControl(uint32_t, uint32_t off) {
    auto& regs = emu_.Get<SiemensMp377Sm501Regs>();
    switch (off) {
    case kResetReg: return enabled_ ? 1u : 0u;
    case kModeReg: return regs.regs_[kModeReg / 4u] & 0xFFu;
    case kToCpuIrqReg: {
        const uint32_t token = to_cpu_token_;
        ClearProtocolInterrupt();
        return token;
    }
    case kFromCpuIrqReg: return from_cpu_token_;
    default: emu_.Get<Fatal>().Die("[MP377 SM501 audio MCU] read of unmodelled control register 0x%X", off);
    }
}

uint32_t SiemensMp377Sm501AudioMcu::ReadSramWord(uint32_t address, uint32_t off) {
    auto& regs = emu_.Get<SiemensMp377Sm501Regs>();
    RunMmioSlice(1024u);
    if (IsProgramSram(off) && IsEnabled()) {
        LOG(Caution,
            "MP377 SM501 8051 SRAM: blocked host read while 8051 enabled pa=0x%08X off=0x%06X reset=0x%08X "
            "mode=0x%08X\n",
            address, off, regs.regs_[kResetReg / 4u], regs.regs_[kModeReg / 4u]);
        regs.HaltUnsupportedAccess("SM501 8051 program/data SRAM read while 8051 enabled", address, 0u);
    }
    uint32_t value = regs.regs_[off / 4u];
    if ((off & ~3u) == (kMailboxBusy & ~3u) && mailbox_busy_reads_ != 0u) {
        value |= 1u << ((kMailboxBusy & 3u) * 8u);
        --mailbox_busy_reads_;
        if (mailbox_busy_reads_ == 0u) SetAudioByte(kMailboxBusy, 0u);
    }
    if ((off & ~3u) == (kMailboxIrqType & ~3u) && ((value >> ((kMailboxIrqType & 3u) * 8u)) & 0x03u) != 0u) {
        emu_.Get<SiemensMp377Sm501AudioOutput>().NoteIrqTypeRead();
    }
    return value;
}

void SiemensMp377Sm501AudioMcu::WriteControl(uint32_t off, uint32_t old_value, uint32_t value) {
    auto& regs = emu_.Get<SiemensMp377Sm501Regs>();
    switch (off) {
    case kResetReg: {
        const bool old_enabled = enabled_;
        enabled_ = (value & 1u) != 0u;
        regs.regs_[off / 4u] = enabled_ ? 1u : 0u;
        if (!enabled_) {
            from_cpu_irq_pending_ = false;
            from_cpu_token_ = 0u;
            to_cpu_token_ = 0u;
            regs.regs_[kToCpuIrqReg / 4u] = 0u;
            regs.regs_[kFromCpuIrqReg / 4u] = 0u;
            SetAudioByte(kMailboxReady, 0u);
            SetAudioByte(kMailboxBusy, 0u);
            SetAudioByte(kMailboxStatus, 0u);
            mailbox_busy_reads_ = 0u;
            regs.ClearSm501InterruptBits(kOutputIrqBit);
            Reset();
        } else if (!old_enabled) {
            Reset();
            SetAudioByte(kMailboxReady, 1u);
            SetAudioByte(kMailboxBusy, 0u);
            SetAudioByte(kMailboxStatus, 0u);
        }
        break;
    }
    case kModeReg: regs.regs_[off / 4u] = value & 0xFFu; break;
    case kToCpuIrqReg: regs.regs_[off / 4u] = old_value; break;
    case kFromCpuIrqReg:
        from_cpu_token_ = value;
        from_cpu_irq_pending_ = true;
        core_.SignalExternalInterrupt0();
        regs.regs_[off / 4u] = value;
        RunMmioSlice(4096u);
        if (AudioByte(kMailboxCmd) != 0u && AudioByte(kMailboxStatus) == 0u) {
            emu_.Get<Fatal>().Die(
                "[MP377 SM501 audio MCU] firmware left mailbox command pending cmd=0x%02X token=0x%08X pc=0x%04X",
                AudioByte(kMailboxCmd), value, core_.ProgramCounter());
        }
        break;
    default:
        emu_.Get<Fatal>().Die("[MP377 SM501 audio MCU] write of unmodelled control register "
                              "0x%X = 0x%08X",
                              off, value);
    }
}

void SiemensMp377Sm501AudioMcu::WriteSramWord(uint32_t address, uint32_t off, uint32_t old_value, uint32_t value) {
    auto& regs = emu_.Get<SiemensMp377Sm501Regs>();
    if (IsProgramSram(off) && IsEnabled()) {
        regs.regs_[off / 4u] = old_value;
        LOG(Caution,
            "MP377 SM501 8051 SRAM: blocked host write while 8051 enabled pa=0x%08X off=0x%06X old=0x%08X new=0x%08X "
            "reset=0x%08X mode=0x%08X\n",
            address, off, old_value, value, regs.regs_[kResetReg / 4u], regs.regs_[kModeReg / 4u]);
        regs.HaltUnsupportedAccess("SM501 8051 program/data SRAM write while 8051 enabled", address, value);
    }
    if (IsPcmBuffer(off)) emu_.Get<SiemensMp377Sm501AudioOutput>().QueueBuffer(off);
}

uint8_t SiemensMp377Sm501AudioMcu::ReadSramByte(uint32_t offset) const {
    return AudioByte(offset);
}

void SiemensMp377Sm501AudioMcu::WriteSramByte(uint32_t offset, uint8_t value) {
    SetAudioByte(offset, value);
}

void SiemensMp377Sm501AudioMcu::Reset() {
    core_.Reset();
}

void SiemensMp377Sm501AudioMcu::RunSlice(uint32_t budget) {
    if (!enabled_ || !emu_.Get<SiemensMp377Sm501PowerGpio>().IsGateEnabled(kSm501Gate8051SramBit) || core_.Halted())
        return;
    if (budget > 65536u) budget = 65536u;
    for (uint32_t i = 0u; i < budget; ++i) {
        if (!core_.RunInstruction(*this, from_cpu_irq_pending_)) break;
    }
}

void SiemensMp377Sm501AudioMcu::RunMmioSlice(uint32_t budget) {
    if (emu_.Get<SiemensMp377Sm501AudioOutput>().IsActive()) return;
    RunSlice(budget);
}

void SiemensMp377Sm501AudioMcu::SaveState(StateWriter& w) const {
    core_.SaveState(w);
    w.Write(enabled_);
    w.Write(from_cpu_irq_pending_);
    w.Write(to_cpu_token_);
    w.Write(from_cpu_token_);
    w.Write(mailbox_busy_reads_);
}

void SiemensMp377Sm501AudioMcu::RestoreState(StateReader& r) {
    core_.RestoreState(r);
    r.Read(enabled_);
    r.Read(from_cpu_irq_pending_);
    r.Read(to_cpu_token_);
    r.Read(from_cpu_token_);
    r.Read(mailbox_busy_reads_);
}

uint8_t SiemensMp377Sm501AudioMcu::FetchCode(uint16_t address) const {
    const auto& regs = emu_.Get<SiemensMp377Sm501Regs>();
    /* SM501 Databook B-1, Figure 12-2 maps the controller's complete code
       address space to the 16 KiB SRAM at internal 8051 addresses
       0x0000..0x3FFF. The embedded core therefore exposes fourteen SRAM
       address bits; a 16-bit MCS-51 PC wraps on that physical code window. */
    const uint16_t sram_address = static_cast<uint16_t>(address & 0x3FFFu);
    return static_cast<uint8_t>(regs.regs_[(kProgramBase + sram_address) / 4u] >> ((sram_address & 3u) * 8u));
}

uint8_t SiemensMp377Sm501AudioMcu::ReadExternal(uint16_t address) {
    auto& regs = emu_.Get<SiemensMp377Sm501Regs>();
    if (address < 0x4000u) return AudioByte(kProgramBase + address);
    if (address >= 0x9004u && address < 0x9010u) {
        const uint32_t byte = address & 3u;
        switch (address & ~3u) {
        case 0x9004u: return static_cast<uint8_t>(regs.regs_[kModeReg / 4u] >> (byte * 8u));
        case 0x9008u: return static_cast<uint8_t>(to_cpu_token_ >> (byte * 8u));
        case 0x900Cu: {
            const uint8_t value = static_cast<uint8_t>(from_cpu_token_ >> (byte * 8u));
            if (byte == 0u) {
                from_cpu_irq_pending_ = false;
                core_.ClearExternalInterrupt0();
                regs.regs_[kFromCpuIrqReg / 4u] = 0u;
            }
            return value;
        }
        }
    }
    if (address >= 0x9100u && address < 0x9184u) {
        return emu_.Get<SiemensMp377Sm501Ac97>().ReadByte(SiemensMp377Sm501Ac97::kBase + address - 0x9100u,
                                                          address == 0x9181u);
    }
    if (address >= 0x9200u && address < 0x9218u) return AudioByte(0x0A0200u + address - 0x9200u);
    emu_.Get<Fatal>().Die("[MP377 SM501 8051] XREAD of unmodelled address 0x%04X at PC 0x%04X", address,
                          core_.ProgramCounter());
}

void SiemensMp377Sm501AudioMcu::WriteExternal(uint16_t address, uint8_t value) {
    auto& regs = emu_.Get<SiemensMp377Sm501Regs>();
    if (address < 0x4000u) {
        SetAudioByte(kProgramBase + address, value);
        return;
    }
    if (address >= 0x9004u && address < 0x9010u) {
        const uint32_t byte = address & 3u;
        switch (address & ~3u) {
        case 0x9004u: {
            uint32_t mode = regs.regs_[kModeReg / 4u];
            mode = (mode & ~(0xFFu << (byte * 8u))) | (static_cast<uint32_t>(value) << (byte * 8u));
            regs.regs_[kModeReg / 4u] = mode & 0xFFu;
            return;
        }
        case 0x9008u:
            to_cpu_token_ = (to_cpu_token_ & ~(0xFFu << (byte * 8u))) | (static_cast<uint32_t>(value) << (byte * 8u));
            regs.regs_[kToCpuIrqReg / 4u] = to_cpu_token_;
            if (byte == 0u) {
                const uint8_t bit = static_cast<uint8_t>((to_cpu_token_ >> 4u) & 3u);
                if (bit != 0u) emu_.Get<SiemensMp377Sm501AudioOutput>().ConsumeIrqBit(bit);
                regs.RaiseSm501InterruptBits(kOutputIrqBit);
            }
            return;
        case 0x900Cu: return;
        }
    }
    if (address >= 0x9100u && address < 0x9184u) {
        emu_.Get<SiemensMp377Sm501Ac97>().WriteByte(SiemensMp377Sm501Ac97::kBase + address - 0x9100u, value);
        return;
    }
    if (address >= 0x9200u && address < 0x9218u) {
        SetAudioByte(0x0A0200u + address - 0x9200u, value);
        return;
    }
    emu_.Get<Fatal>().Die("[MP377 SM501 8051] XWRITE of unmodelled address 0x%04X = 0x%02X "
                          "at PC 0x%04X",
                          address, value, core_.ProgramCounter());
}

uint8_t SiemensMp377Sm501AudioMcu::AudioByte(uint32_t off) const {
    const auto& regs = emu_.Get<SiemensMp377Sm501Regs>();
    return static_cast<uint8_t>(regs.regs_[(off & ~3u) / 4u] >> ((off & 3u) * 8u));
}

void SiemensMp377Sm501AudioMcu::SetAudioByte(uint32_t off, uint8_t value) {
    auto& word = emu_.Get<SiemensMp377Sm501Regs>().regs_[(off & ~3u) / 4u];
    const uint32_t shift = (off & 3u) * 8u;
    word = (word & ~(0xFFu << shift)) | (static_cast<uint32_t>(value) << shift);
}

void SiemensMp377Sm501AudioMcu::RaiseOutputIrq(uint8_t buffer_bit) {
    buffer_bit &= 3u;
    if (buffer_bit == 0u) return;
    auto& regs = emu_.Get<SiemensMp377Sm501Regs>();
    SetAudioByte(kMailboxIrqType, buffer_bit);
    to_cpu_token_ = (to_cpu_token_ & ~0xF0u) | static_cast<uint8_t>(buffer_bit << 4u);
    regs.regs_[kToCpuIrqReg / 4u] = to_cpu_token_;
    regs.RaiseSm501InterruptBits(kOutputIrqBit);
    emu_.Get<IrqController>().AssertIrq(kMp377AudioIrqSource);
}

bool SiemensMp377Sm501AudioMcu::OutputTokenPending() const {
    return to_cpu_token_ != 0u;
}

void SiemensMp377Sm501AudioMcu::AcknowledgeInterrupts(uint32_t value) {
    auto& regs = emu_.Get<SiemensMp377Sm501Regs>();
    const uint32_t ack = value & (kOutputIrqBit | kSm501Ac97IrqBit | kI2sIrqBit);
    if ((ack & kOutputIrqBit) != 0u) {
        regs.ClearSm501InterruptBits(kOutputIrqBit);
        SetAudioByte(kMailboxIrqType, static_cast<uint8_t>(AudioByte(kMailboxIrqType) & 0xF0u));
        to_cpu_token_ &= ~0xF0u;
        regs.regs_[kToCpuIrqReg / 4u] = to_cpu_token_;
        emu_.Get<IrqController>().DeAssertIrq(kMp377AudioIrqSource);
    }
    if ((ack & kSm501Ac97IrqBit) != 0u) emu_.Get<SiemensMp377Sm501Ac97>().ClearInterrupt();
    regs.ClearSm501InterruptBits(ack & ~(kOutputIrqBit | kSm501Ac97IrqBit));
}

void SiemensMp377Sm501AudioMcu::ClearProtocolInterrupt() {
    auto& regs = emu_.Get<SiemensMp377Sm501Regs>();
    regs.ClearSm501InterruptBits(kOutputIrqBit);
    regs.regs_[kToCpuIrqReg / 4u] = 0u;
    to_cpu_token_ = 0u;
    emu_.Get<IrqController>().DeAssertIrq(kMp377AudioIrqSource);
}

REGISTER_SERVICE(SiemensMp377Sm501AudioMcu);

} // namespace siemens_mp377
