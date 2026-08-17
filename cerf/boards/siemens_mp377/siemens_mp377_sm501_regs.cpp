#define NOMINMAX

#include "siemens_mp377_sm501_internal.h"
#include "siemens_mp377_sm501_blitter.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/irq_controller.h"
#include "../../socs/iop13xx/iop13xx_atu.h"
#include "../../state/state_stream.h"

#include <algorithm>
#include <cstdint>

namespace siemens_mp377 {

namespace {


/* MP377 nk.exe BSPIntrInit calls OALIntrStaticTranslate(31, 10), and
   VGXaudio.dll sub_298913C passes SYSINTR 31 to InterruptInitialize().
   Raw source 0x23 belongs to touch (SYSINTR 0x1B), so audio completion
   must use the SM501 PCI interrupt line 0x0A instead of the touch GPIO. */
constexpr int kMp377AudioIrqSource = 10;

uint32_t Sm501RegsAtuAliasRead(void* ctx, uint32_t off, uint32_t width) {
    auto* regs = static_cast<SiemensMp377Sm501Regs*>(ctx);
    const uint32_t a = kSm501RegsBarBus + off;
    switch (width) {
        case 1: return regs->ReadByte(a);
        case 2: return regs->ReadHalf(a);
        case 4: return regs->ReadWord(a);
    }
    LOG(Caution, "MP377 ATU OUTBOUND SM501: unsupported BAR1 read width=%u off=0x%08X\n",
        width, off);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void Sm501RegsAtuAliasWrite(void* ctx, uint32_t off, uint32_t value, uint32_t width) {
    auto* regs = static_cast<SiemensMp377Sm501Regs*>(ctx);
    const uint32_t a = kSm501RegsBarBus + off;
    switch (width) {
        case 1: regs->WriteByte(a, static_cast<uint8_t>(value)); return;
        case 2: regs->WriteHalf(a, static_cast<uint16_t>(value)); return;
        case 4: regs->WriteWord(a, value); return;
    }
    LOG(Caution, "MP377 ATU OUTBOUND SM501: unsupported BAR1 write width=%u off=0x%08X value=0x%08X\n",
        width, off, value);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

bool Sm501ComputeAtuCpuBase(CerfEmulator& emu,
                            const char* name,
                            uint32_t pci_bar,
                            uint32_t bytes,
                            uint64_t& cpu_base,
                            uint32_t& window) {
    window = 0xFFFFFFFFu;
    if (!emu.Get<Iop13xxAtuState>().PciMemBusToCpuPhys(
            pci_bar, bytes, cpu_base, &window, false)) {
        LOG(Caution,
            "MP377 ATU OUTBOUND SM501: %s pci=0x%08X..0x%08X translation unavailable; high-PA alias NOT registered\n",
            name, pci_bar, pci_bar + bytes);
        return false;
    }
    return true;
}

}  // namespace

bool SiemensMp377Sm501Regs::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::SiemensMP377;
}

void SiemensMp377Sm501Regs::OnReady() {
    regs_.assign(kSm501RegsBytes / 4u, 0u);
    emu_.Get<SiemensMp377Sm501PowerGpio>().Initialize(*this);
    sm501_irq_line_asserted_ = false;
    auto& disp = emu_.Get<PeripheralDispatcher>();
    disp.Register(this);

    uint64_t atu_cpu_base = 0;
    uint32_t atu_window = 0xFFFFFFFFu;
    if (Sm501ComputeAtuCpuBase(emu_, "BAR1-regs", kSm501RegsBarBus,
                               kSm501RegsBytes, atu_cpu_base, atu_window)) {
        disp.RegisterAlias(this, atu_cpu_base, kSm501RegsBytes,
                           &Sm501RegsAtuAliasRead,
                           &Sm501RegsAtuAliasWrite,
                           this);
    }
}

uint32_t SiemensMp377Sm501Regs::MmioBase() const { return kSm501RegsBarPa; }

uint32_t SiemensMp377Sm501Regs::MmioSize() const { return kSm501RegsBytes; }

uint32_t SiemensMp377Sm501Regs::PanelFbOffset() const {
    return NormalizePanelFbOffset(panel_fb_raw_);
}

uint32_t SiemensMp377Sm501Regs::PanelPitchBytes() const {
    return panel_pitch_bytes_ ? panel_pitch_bytes_ : kFbStride;
}

uint32_t SiemensMp377Sm501Regs::ReadSm501Register(uint32_t offset) const {
    return regs_[offset / 4u];
}

uint8_t SiemensMp377Sm501Regs::ReadByte(uint32_t a) {
    return static_cast<uint8_t>(ReadWord(a & ~3u) >> ((a & 3u) * 8u));
}

uint16_t SiemensMp377Sm501Regs::ReadHalf(uint32_t a) {
    return static_cast<uint16_t>(ReadWord(a & ~3u) >> ((a & 2u) * 8u));
}

uint32_t SiemensMp377Sm501Regs::ReadWord(uint32_t a) {
    uint32_t off = 0;
    if (!Sm501RegsPaToOffset(a, off))
        HaltUnsupportedAccess("SM501 regs read outside BAR1", a, 0);
    if (SiemensMp377Sm501Dma::IsRegister(off)) {
        emu_.Get<SiemensMp377Sm501AudioMcu>().RunMmioSlice(1024u);
        const uint32_t value = emu_.Get<SiemensMp377Sm501Dma>().Read(off);
        return value;
    }

    if (SiemensMp377Sm501Ac97::IsRegister(off)) {
        emu_.Get<SiemensMp377Sm501AudioMcu>().RunMmioSlice(1024u);
        const uint32_t value =
            emu_.Get<SiemensMp377Sm501Ac97>().Read(off, true);
        return value;
    }

    if (SiemensMp377Sm501AudioMcu::IsControlRegister(off)) {
        auto& mcu = emu_.Get<SiemensMp377Sm501AudioMcu>();
        mcu.RunMmioSlice(4096u);
        const uint32_t value = mcu.ReadControl(a, off);
        return value;
    }

    if (SiemensMp377Sm501AudioMcu::IsSram(off)) {
        return emu_.Get<SiemensMp377Sm501AudioMcu>().ReadSramWord(a, off);
    }

    uint32_t value = 0u;
    switch (off) {
    case kSm501CommandListStatusReg: value = kSm501CommandListIdle; break;
    case kSm501RawIrqStatusReg:
        value = regs_[kSm501IrqStatusReg / 4u] & kSm501SupportedIrqBits;
        break;
    case kSm501IrqStatusReg:
        value = emu_.Get<SiemensMp377SmiBridge>().Sm501MasterStatus() |
                Sm501LatchedInterruptStatus();
        break;
    case kSm501IrqMaskReg:
        value = regs_[kSm501IrqMaskReg / 4u] & kSm501SupportedIrqBits;
        break;
    case kSm501CurrentGateReg:
        value = emu_.Get<SiemensMp377Sm501PowerGpio>().CurrentGate(); break;
    case kSm501CurrentClockReg:
        value = emu_.Get<SiemensMp377Sm501PowerGpio>().CurrentClock(); break;
    case kSm501PowerMode0GateReg:
    case kSm501PowerMode0ClockReg:
    case kSm501PowerMode1GateReg:
    case kSm501PowerMode1ClockReg:
    case kSm501SleepModeGateReg:
    case kSm501PowerModeControlReg:
        value = regs_[off / 4u];
        break;
    case kSm501GpioDataLowReg:
        value = emu_.Get<SiemensMp377Sm501PowerGpio>().ReadGpioDataLow(); break;
    case kSm501GpioDirectionLowReg:
        value = emu_.Get<SiemensMp377Sm501PowerGpio>().ReadGpioDirectionLow(); break;
    case kSm501DeviceIdReg: value = kSm501DeviceId; break;
    case 0x020008u: case 0x020108u: {
        value = emu_.Get<SiemensMp377TouchPanel>().ReadSmiSampleWord();
        break;
    }
    case 0x02000Cu: case 0x02010Cu: {
        /* smibase sub_2B53FDC completes a TX only when:
               (SMI_INT_STATUS & 2) && (SMI_STATUS & 3).
           While the driver has ctrl bit 1 set, expose one TX-ready bit
           along with the stable status bits. */
        const uint32_t ctrl_off = (off == 0x02010Cu) ? 0x020104u : 0x020004u;
        const bool tx_active = (regs_[ctrl_off / 4u] & 0x2u) != 0u;
        value = tx_active ? 0x0000000Du : 0x0000000Cu;
        break;
    }
    case 0x020014u: case 0x020114u:
        /* smibase sub_2B53FDC treats INT_STATUS bit 0x4 as RX/FIFO error.
           Expose only the TX-completion bit here; ADC reads use the data
           register path, not the RX/error interrupt bit. */
        value = 0x00000002u;
        break;
    default:
        value = regs_[off / 4u];
        break;
    }
    return value;
}

void SiemensMp377Sm501Regs::WriteByte(uint32_t a, uint8_t v) {
    uint32_t off = 0;
    if (!Sm501RegsPaToOffset(a, off))
        HaltUnsupportedAccess("SM501 regs byte write outside BAR1", a, v);
    const uint32_t shift = (off & 3u) * 8u;
    uint32_t w = regs_[off / 4u];
    w = (w & ~(0xFFu << shift)) | (static_cast<uint32_t>(v) << shift);
    WriteWord(a & ~3u, w);
}

void SiemensMp377Sm501Regs::WriteHalf(uint32_t a, uint16_t v) {
    uint32_t off = 0;
    if (!Sm501RegsPaToOffset(a, off))
        HaltUnsupportedAccess("SM501 regs half write outside BAR1", a, v);
    const uint32_t shift = (off & 2u) * 8u;
    uint32_t w = regs_[off / 4u];
    w = (w & ~(0xFFFFu << shift)) | (static_cast<uint32_t>(v) << shift);
    WriteWord(a & ~3u, w);
}

void SiemensMp377Sm501Regs::WriteWord(uint32_t a, uint32_t v) {
    uint32_t off = 0;
    if (!Sm501RegsPaToOffset(a, off))
        HaltUnsupportedAccess("SM501 regs word write outside BAR1", a, v);
    const uint32_t old_value = regs_[off / 4u];
    regs_[off / 4u] = v;

    if (off == kSm501IrqMaskReg) {
        regs_[off / 4u] = v & kSm501SupportedIrqBits;
        RefreshSm501InterruptLine();
        return;
    }

    if (off == kSm501RawIrqStatusReg || off == kSm501IrqStatusReg) {
        /* 0x00002C is documented read-only, but the MP377 driver uses
           write-one-to-clear style acks for the audio-related sources.
           Keep the latched status stable except for the bits explicitly acked. */
        const uint32_t old_status = regs_[kSm501IrqStatusReg / 4u];
        regs_[off / 4u] = old_value;
        regs_[kSm501IrqStatusReg / 4u] = old_status;
        emu_.Get<SiemensMp377Sm501AudioMcu>().AcknowledgeInterrupts(v);
        if ((v & SiemensMp377SmiBridge::kSm501MasterBit) != 0u) RefreshSm501InterruptLine();
        return;
    }

    if (SiemensMp377Sm501Dma::IsRegister(off)) {
        emu_.Get<SiemensMp377Sm501Dma>().Write(off, v);
        emu_.Get<SiemensMp377Sm501AudioMcu>().RunMmioSlice(4096u);
        return;
    }

    if (off == kSm501CurrentGateReg || off == kSm501CurrentClockReg) {
        /* Current Gate/Clock are read-only mirrors of the selected power mode. */
        regs_[off / 4u] = old_value;
        return;
    }

    if (off == kSm501PowerModeControlReg) {
        emu_.Get<SiemensMp377Sm501PowerGpio>().WritePowerModeControl(v);
        emu_.Get<SiemensMp377Sm501PowerGpio>().UpdateAc97Link();
    } else if (off == kSm501PowerMode0GateReg || off == kSm501PowerMode0ClockReg ||
               off == kSm501PowerMode1GateReg || off == kSm501PowerMode1ClockReg ||
               off == kSm501SleepModeGateReg || off == kSm501Gpio31_0ControlReg ||
               off == kSm501GpioDataLowReg || off == kSm501GpioDirectionLowReg) {
        emu_.Get<SiemensMp377Sm501PowerGpio>().UpdateAc97Link();
    }

    if (SiemensMp377Sm501Ac97::IsRegister(off)) {
        emu_.Get<SiemensMp377Sm501Ac97>().Write(off, old_value, v);
        emu_.Get<SiemensMp377Sm501AudioMcu>().RunMmioSlice(4096u);
        return;
    }

    if (SiemensMp377Sm501AudioMcu::IsControlRegister(off)) {
        auto& mcu = emu_.Get<SiemensMp377Sm501AudioMcu>();
        mcu.WriteControl(off, old_value, v);
        mcu.RunMmioSlice(4096u);
        return;
    }

    if (SiemensMp377Sm501AudioMcu::IsSram(off)) {
        emu_.Get<SiemensMp377Sm501AudioMcu>().WriteSramWord(
            a, off, old_value, v);
    }


    if ((off == 0x020004u || off == 0x020104u) &&
        ((old_value & 0x2u) == 0u) && ((v & 0x2u) != 0u)) {
        emu_.Get<SiemensMp377SmiBridge>().AssertPending();
    }

    if ((off == 0x020004u || off == 0x020104u) &&
        ((old_value & 0x2u) != 0u) && ((v & 0x2u) == 0u)) {
        /* smibase sub_2B53FDC completes TX by clearing CTRL bit 1 before
           setting tx_event.  Model that falling edge as hardware TX IRQ
           completion/ack as well, otherwise CP6 source 24 can remain
           pending and starve lower-priority touch IRQ 0x23. */
        emu_.Get<SiemensMp377SmiBridge>().ClearPending();
    }


    if ((off == 0x020014u || off == 0x020114u) && (v & 0x2u) != 0u) {
        emu_.Get<SiemensMp377SmiBridge>().ClearPending();
    }

    if (off == 0x08000Cu || off == 0x080044u) {
        panel_fb_raw_ = v;
    } else if (off == 0x080010u || off == 0x080014u || off == 0x080048u) {
        panel_pitch_bytes_ = DecodePanelPitchBytes(v);
    }

    if (off == 0x020008u || off == 0x020108u) {
        emu_.Get<SiemensMp377TouchPanel>().QueueSmiCommand(static_cast<uint16_t>(v & 0xFFFFu));
    }
    if (SiemensMp377Sm501Blitter::IsCommandRegister(off) &&
        (v & 0xC0000000u)) {
        emu_.Get<SiemensMp377Sm501Blitter>().ExecuteCommand(v);
    }
    if (SiemensMp377Sm501Blitter::IsDataPort(off))
        emu_.Get<SiemensMp377Sm501Blitter>().WriteDataPort(v);

}

void SiemensMp377Sm501Regs::SaveState(StateWriter& w) {
    /* The AC-link pacer raises IRQs from its host thread.  Freeze it while the
       device image is serialized so the mailbox/token/FIFO snapshot is
       internally consistent.  AudioPacerLoop uses this same mutex for every
       tick. */
    auto audio_pacer_lock =
        emu_.Get<SiemensMp377Sm501AudioOutput>().LockForState();

    WriteVectorState(w, regs_);
    w.Write(panel_fb_raw_);
    w.Write(panel_pitch_bytes_);

    emu_.Get<SiemensMp377Sm501Blitter>().SaveState(w);

    emu_.Get<SiemensMp377Sm501Ac97>().SaveState(w);
    w.Write(sm501_irq_line_asserted_);
    emu_.Get<SiemensMp377Sm501AudioMcu>().SaveState(w);
    emu_.Get<SiemensMp377Sm501AudioOutput>().SaveState(w);

    emu_.Get<SiemensMp377TouchPanel>().SaveState(w);
}

void SiemensMp377Sm501Regs::RestoreState(StateReader& r) {
    /* Stop guest buffer completions before replacing mailbox/FIFO state. */
    emu_.Get<SiemensMp377Sm501AudioOutput>().SetPacerEnabled(false);

    ReadVectorState(r, regs_, kSm501RegsBytes / 4u, "SM501 register state size");
    if (regs_.size() != kSm501RegsBytes / 4u)
        HaltUnsupportedAccess("SM501 register state size", kSm501RegsBarPa, regs_.size());
    r.Read(panel_fb_raw_);
    r.Read(panel_pitch_bytes_);

    emu_.Get<SiemensMp377Sm501Blitter>().RestoreState(r);

    emu_.Get<SiemensMp377Sm501Ac97>().RestoreState(r);
    r.Read(sm501_irq_line_asserted_);
    emu_.Get<SiemensMp377Sm501AudioMcu>().RestoreState(r);
    emu_.Get<SiemensMp377Sm501AudioOutput>().RestoreState(r);

    emu_.Get<SiemensMp377TouchPanel>().RestoreState(r);
}




uint32_t SiemensMp377Sm501Regs::NormalizePanelFbOffset(uint32_t v) {
    uint32_t off = 0;
    if (Sm501FbPaToOffset(v, off))
        return off;

    off = v & 0x03FFFFFFu;
    return off < kSm501FbBytes ? off : 0u;
}

uint32_t SiemensMp377Sm501Regs::DecodePanelPitchBytes(uint32_t v) {
    uint32_t p = v & 0x3FFFu;
    if (!p) p = (v >> 16) & 0x3FFFu;
    if (!p) return 0u;

    /* Early OAL/DDI code can expose the panel pitch either as pixels or
       bytes.  Interpret values up to the HWI-selected panel width as
       RGB565 pixels; larger values are already byte pitches. */
    if (p <= kFbWidth) p *= 2u;

    if (p < 2u || p > kSm501FbBytes / 2u) return 0u;
    return p;
}

uint32_t SiemensMp377Sm501Regs::Lo16(uint32_t v) { return v & 0xFFFFu; }

uint32_t SiemensMp377Sm501Regs::Hi16(uint32_t v) { return (v >> 16) & 0xFFFFu; }

uint32_t SiemensMp377Sm501Regs::Sm501LatchedInterruptStatus() const {
    return regs_[kSm501IrqStatusReg / 4u] & kSm501SupportedIrqBits;
}

void SiemensMp377Sm501Regs::RefreshSm501InterruptLine() {
    const uint32_t active = Sm501LatchedInterruptStatus() & regs_[kSm501IrqMaskReg / 4u];
    if (active != 0u) {
        sm501_irq_line_asserted_ = true;
        emu_.Get<SiemensMp377SmiBridge>().AssertPending();
    } else if (sm501_irq_line_asserted_) {
        sm501_irq_line_asserted_ = false;
        emu_.Get<SiemensMp377SmiBridge>().ClearPending();
    }
}

void SiemensMp377Sm501Regs::RaiseSm501InterruptBits(uint32_t bits) {
    bits &= kSm501SupportedIrqBits;
    if (bits == 0u) return;
    const uint32_t old_status = regs_[kSm501IrqStatusReg / 4u];
    regs_[kSm501IrqStatusReg / 4u] = old_status | bits;
    RefreshSm501InterruptLine();
}

void SiemensMp377Sm501Regs::ClearSm501InterruptBits(uint32_t bits) {
    bits &= kSm501SupportedIrqBits;
    if (bits == 0u) return;
    regs_[kSm501IrqStatusReg / 4u] &= ~bits;
    RefreshSm501InterruptLine();
}

REGISTER_SERVICE(SiemensMp377Sm501Regs);

} // namespace siemens_mp377

