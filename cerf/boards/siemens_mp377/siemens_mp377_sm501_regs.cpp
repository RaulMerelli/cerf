#define NOMINMAX

#include "siemens_mp377_sm501_internal.h"
#include "siemens_mp377_sm501_regs.h"
#include "siemens_mp377_sm501_blitter.h"
#include "siemens_mp377_sm501_register_map.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/irq_controller.h"
#include "../../state/state_stream.h"

#include <algorithm>
#include <cstdint>

namespace siemens_mp377 {

namespace {

/* MP377 nk.exe BSPIntrInit calls OALIntrStaticTranslate(31, 10), and
   siemens_mp377_v1040 VGXaudio.dll sub_298913C passes SYSINTR 31 to InterruptInitialize().
   Raw source 0x23 belongs to touch (SYSINTR 0x1B), so audio completion
   must use the SM501 PCI interrupt line 0x0A instead of the touch GPIO. */
constexpr int kMp377AudioIrqSource = 10;

} // namespace

bool SiemensMp377Sm501Regs::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::SiemensMP377;
}

void SiemensMp377Sm501Regs::OnReady() {
    regs_.assign(kSm501RegsBytes / 4u, 0u);
    emu_.Get<SiemensMp377Sm501PowerGpio>().Initialize(*this);
    sm501_irq_line_asserted_ = false;
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t SiemensMp377Sm501Regs::MmioBase() const {
    return kSm501RegsBarPa;
}

uint32_t SiemensMp377Sm501Regs::MmioSize() const {
    return kSm501RegsBytes;
}

uint32_t SiemensMp377Sm501Regs::PanelFbOffset() const {
    return NormalizePanelFbOffset(panel_fb_raw_);
}

/* SM501 Databook v1.02 section 5, Panel Horizontal Total (MMIO_base +
   0x080024): HDE bits[11:0] is the "panel horizontal display end specified as
   number of pixels - 1".  Power-on default is Undefined, so an unprogrammed
   register reads back 0 and the caller keeps its own fallback. */
uint32_t SiemensMp377Sm501Regs::PanelWidthPixels() const {
    const uint32_t hde = ReadSm501Register(0x080024u) & 0xFFFu;
    return hde ? hde + 1u : 0u;
}

/* SM501 Databook v1.02 section 5, Panel Vertical Total (MMIO_base + 0x08002C):
   VDE bits[10:0] is the "panel vertical display end specified as number of
   lines - 1". */
uint32_t SiemensMp377Sm501Regs::PanelHeightLines() const {
    const uint32_t vde = ReadSm501Register(0x08002Cu) & 0x7FFu;
    return vde ? vde + 1u : 0u;
}

uint32_t SiemensMp377Sm501Regs::PanelPitchBytes() const {
    return panel_pitch_bytes_ ? panel_pitch_bytes_ : kFbStride;
}

uint32_t SiemensMp377Sm501Regs::ReadSm501Register(uint32_t offset) const {
    return regs_[offset / 4u];
}

/* The MP377 guest drivers address every non-2D block through the upper half
   of the 2 MB BAR1: touch IRQ mask at 0x100030, IRQ status at 0x10002C,
   GPIO at 0x100008, SSP at 0x120004, panel DC at 0x18000C, 8051 SRAM mailbox
   at 0x1C3FF0 — the upper half mirrors the lower 1 MB register file (SM501
   device: PCI 126F:0501, Linux sm501-regs.h block map).  Driver side:
   smibase sub_2B51544 identifies 126F:0501 rev 0xA0, takes SYSINTR 46, and
   maps 2 MB through TransBusAddrToStatic; sub_2B51E5C then ORs its IRQ mask
   at regs_base + 0x30, so the +0x100000 lives inside the static-mapped base
   the kernel handed the driver.  The 2D engine lives natively in the upper
   half (sm501-regs.h SM501_2D_ENGINE 0x100000..0x100050, CSC 0xC8..0xFC,
   SM501_2D_ENGINE_DATA 0x110000).  Direction split inside the overlap: the
   guest issues 2D state/command WRITES there (ddi_vgx sub_2992E70 stores
   Source/Dimension/Control 0xC0000000-trigger, sub_2997CF8 stores Stretch)
   but issues system-block READS through the same offsets, observed live:
   sub_2992E70 stores the 2D Source/Dimension/Control 0xC0000000-trigger
   (write, native), siemens_mp377_v1040 ceddk.dll READ_REGISTER_ULONG (0x3D523DC) reads GPIO
   0x100008 on ddi_vgx's behalf, ddi_vgx sub_2997098 polls the command-list
   status 0x100024 (0x029970A4), smibase reads mask 0x100030 (0x02B51F38)
   and status 0x10002C (0x02B5198C); ddi_vgx sub_2997CF8 reads its 2D
   stretch word 0x10001C natively through VA 0xB410xxxx. */
uint8_t SiemensMp377Sm501Regs::ReadByte(uint32_t a) {
    const uint32_t fa = Sm501FoldRead(a);
    return static_cast<uint8_t>(ReadWord(fa & ~3u) >> ((fa & 3u) * 8u));
}

uint16_t SiemensMp377Sm501Regs::ReadHalf(uint32_t a) {
    const uint32_t fa = Sm501FoldRead(a);
    return static_cast<uint16_t>(ReadWord(fa & ~3u) >> ((fa & 2u) * 8u));
}

uint32_t SiemensMp377Sm501Regs::ReadWord(uint32_t a) {
    uint32_t off = 0;
    if (!Sm501RegsPaToOffset(a, off)) HaltUnsupportedAccess("SM501 regs read outside BAR1", a, 0);
    if (off >= 0x100000u && off < 0x200000u && !Sm501Native2dRead(off)) off -= 0x100000u;
    if (SiemensMp377Sm501Dma::IsRegister(off)) {
        emu_.Get<SiemensMp377Sm501AudioMcu>().RunMmioSlice(1024u);
        const uint32_t value = emu_.Get<SiemensMp377Sm501Dma>().Read(off);
        return value;
    }

    if (SiemensMp377Sm501Ac97::IsRegister(off)) {
        emu_.Get<SiemensMp377Sm501AudioMcu>().RunMmioSlice(1024u);
        const uint32_t value = emu_.Get<SiemensMp377Sm501Ac97>().Read(off, true);
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
    case kSm501RawIrqStatusReg: value = regs_[kSm501IrqStatusReg / 4u] & kSm501SupportedIrqBits; break;
    case kSm501IrqStatusReg:
        value = emu_.Get<SiemensMp377SmiBridge>().Sm501MasterStatus() | Sm501LatchedInterruptStatus();
        break;
    case kSm501IrqMaskReg: value = regs_[kSm501IrqMaskReg / 4u] & kSm501SupportedIrqBits; break;
    case kSm501CurrentGateReg: value = emu_.Get<SiemensMp377Sm501PowerGpio>().CurrentGate(); break;
    case kSm501CurrentClockReg: value = emu_.Get<SiemensMp377Sm501PowerGpio>().CurrentClock(); break;
    case kSm501PowerMode0GateReg:
    case kSm501PowerMode0ClockReg:
    case kSm501PowerMode1GateReg:
    case kSm501PowerMode1ClockReg:
    case kSm501SleepModeGateReg:
    case kSm501PowerModeControlReg: value = regs_[off / 4u]; break;
    case kSm501GpioDataLowReg: value = emu_.Get<SiemensMp377Sm501PowerGpio>().ReadGpioDataLow(); break;
    case kSm501GpioDirectionLowReg: value = emu_.Get<SiemensMp377Sm501PowerGpio>().ReadGpioDirectionLow(); break;
    case kSm501DeviceIdReg: value = kSm501DeviceId; break;
    case 0x020008u:
    case 0x020108u: {
        value = emu_.Get<SiemensMp377TouchPanel>().ReadSmiSampleWord();
        break;
    }
    case 0x02000Cu:
    case 0x02010Cu: {
        /* smibase sub_2B53FDC completes a TX only when:
               (SMI_INT_STATUS & 2) && (SMI_STATUS & 3).
           While the driver has ctrl bit 1 set, expose one TX-ready bit
           along with the stable status bits. */
        const uint32_t ctrl_off = (off == 0x02010Cu) ? 0x020104u : 0x020004u;
        const bool tx_active = (regs_[ctrl_off / 4u] & 0x2u) != 0u;
        value = tx_active ? 0x0000000Du : 0x0000000Cu;
        break;
    }
    case 0x020014u:
    case 0x020114u:
        /* smibase sub_2B53FDC treats INT_STATUS bit 0x4 as RX/FIFO error.
           Expose only the TX-completion bit here; ADC reads use the data
           register path, not the RX/error interrupt bit. */
        value = 0x00000002u;
        break;
    default:
        if (!Sm501IsPlainRegister(off))
            emu_.Get<Fatal>().Die("[MP377 SM501] read of unmodelled BAR1 register 0x%06X", off);
        value = regs_[off / 4u];
        break;
    }
    return value;
}

void SiemensMp377Sm501Regs::WriteByte(uint32_t a, uint8_t v) {
    const uint32_t fa = Sm501FoldWrite(a);
    uint32_t off = 0;
    if (!Sm501RegsPaToOffset(fa, off)) HaltUnsupportedAccess("SM501 regs byte write outside BAR1", fa, v);
    const uint32_t shift = (fa & 3u) * 8u;
    uint32_t w = regs_[off / 4u];
    w = (w & ~(0xFFu << shift)) | (static_cast<uint32_t>(v) << shift);
    WriteWord(fa & ~3u, w);
}

void SiemensMp377Sm501Regs::WriteHalf(uint32_t a, uint16_t v) {
    const uint32_t fa = Sm501FoldWrite(a);
    uint32_t off = 0;
    if (!Sm501RegsPaToOffset(fa, off)) HaltUnsupportedAccess("SM501 regs half write outside BAR1", fa, v);
    const uint32_t shift = (fa & 2u) * 8u;
    uint32_t w = regs_[off / 4u];
    w = (w & ~(0xFFFFu << shift)) | (static_cast<uint32_t>(v) << shift);
    WriteWord(fa & ~3u, w);
}

void SiemensMp377Sm501Regs::WriteWord(uint32_t a, uint32_t v) {
    uint32_t off = 0;
    if (!Sm501RegsPaToOffset(a, off)) HaltUnsupportedAccess("SM501 regs word write outside BAR1", a, v);
    if (off >= 0x100000u && off < 0x200000u && !Sm501Native2dWrite(off)) off -= 0x100000u;
    if (SiemensMp377Sm501AudioMcu::IsSram(off)) {
        auto& mcu = emu_.Get<SiemensMp377Sm501AudioMcu>();
        const uint32_t old_value = regs_[off / 4u];
        mcu.WriteSramWord(a, off, old_value, v);
        return;
    }
    if (!SiemensMp377Sm501Dma::IsRegister(off) && !SiemensMp377Sm501Ac97::IsRegister(off) &&
        !SiemensMp377Sm501AudioMcu::IsControlRegister(off) && !Sm501IsPlainRegister(off))
        emu_.Get<Fatal>().Die("[MP377 SM501] write of unmodelled BAR1 register 0x%06X = 0x%08X", off, v);
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
    } else if (off == kSm501PowerMode0GateReg || off == kSm501PowerMode0ClockReg || off == kSm501PowerMode1GateReg ||
               off == kSm501PowerMode1ClockReg || off == kSm501SleepModeGateReg || off == kSm501Gpio31_0ControlReg ||
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

    if ((off == 0x020004u || off == 0x020104u) && ((old_value & 0x2u) == 0u) && ((v & 0x2u) != 0u)) {
        emu_.Get<SiemensMp377SmiBridge>().AssertPending();
    }

    if ((off == 0x020004u || off == 0x020104u) && ((old_value & 0x2u) != 0u) && ((v & 0x2u) == 0u)) {
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
    } else if (off == 0x080010u) {
        panel_pitch_bytes_ = DecodePanelPitchBytes(v);
    }

    if (off == 0x020008u || off == 0x020108u) {
        emu_.Get<SiemensMp377TouchPanel>().QueueSmiCommand(static_cast<uint16_t>(v & 0xFFFFu));
    }
    if (SiemensMp377Sm501Blitter::IsCommandRegister(off) && (v & 0xC0000000u)) {
        emu_.Get<SiemensMp377Sm501Blitter>().ExecuteCommand(v);
    }
    if (SiemensMp377Sm501Blitter::IsDataPort(off)) emu_.Get<SiemensMp377Sm501Blitter>().WriteDataPort(v);
}

void SiemensMp377Sm501Regs::SaveState(StateWriter& w) {
    /* The AC-link pacer raises IRQs from its host thread.  Freeze it while the
       device image is serialized so the mailbox/token/FIFO snapshot is
       internally consistent.  AudioPacerLoop uses this same mutex for every
       tick. */
    auto audio_pacer_lock = emu_.Get<SiemensMp377Sm501AudioOutput>().LockForState();

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
    if (Sm501FbPaToOffset(v, off)) return off;

    off = v & 0x03FFFFFFu;
    return off < kSm501FbBytes ? off : 0u;
}

uint32_t SiemensMp377Sm501Regs::DecodePanelPitchBytes(uint32_t v) {
    /* SM501 Databook v1.02 section 5, Panel FB Offset (MMIO_base + 0x080010):
       bits[29:20] "FB Window Width - Number of bytes per line of the frame
       buffer window specified in 128-bit aligned bytes".  Panel FB Width
       (0x080014) carries pixels, and Video FB Width (0x080048) belongs to the
       video plane, so neither is a panel pitch. */
    const uint32_t bytes = ((v >> 20) & 0x3FFu) * 16u;
    if (bytes < 2u || bytes > kSm501FbBytes / 2u) return 0u;
    return bytes;
}

uint32_t SiemensMp377Sm501Regs::Lo16(uint32_t v) {
    return v & 0xFFFFu;
}

uint32_t SiemensMp377Sm501Regs::Hi16(uint32_t v) {
    return (v >> 16) & 0xFFFFu;
}

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
