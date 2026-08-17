#define NOMINMAX

#include "siemens_mp377_sm501_internal.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/irq_controller.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace siemens_mp377 {

/*
 * MP377 SMI bridge windows are board/OAL cascade registers, not SM501
 * BAR0/BAR1 aliases. Keep this model separate from the BAR-relative SM501
 * framebuffer/register apertures; do not route C410/C480 traffic through
 * SiemensMp377Sm501Fb or SiemensMp377Sm501Regs.
 */

bool SiemensMp377SmiBridge::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::SiemensMP377;
}

uint32_t SiemensMp377SmiBridge::Sm501MasterStatus() const {
    return master_pending_.load(std::memory_order_acquire) ? kSm501MasterBit : 0u;
}

void SiemensMp377SmiBridge::AssertCascade() {
    emu_.Get<IrqController>().AssertIrq(kCascadeSource);
}

void SiemensMp377SmiBridge::DeassertCascade() {
    emu_.Get<IrqController>().DeAssertIrq(kCascadeSource);
}

void SiemensMp377SmiBridge::AssertPending() {
    master_pending_.store(1u, std::memory_order_release);
    bridge_status_.fetch_or(kBridgeStatusBit, std::memory_order_acq_rel);
    AssertCascade();
}

void SiemensMp377SmiBridge::ClearPending() {
    master_pending_.store(0u, std::memory_order_release);
    bridge_status_.store(0u, std::memory_order_release);
    DeassertCascade();
}

uint32_t SiemensMp377SmiBridge::Read(uint32_t pa) const {
    switch ((pa & ~3u) & 0x0Fu) {
    case 0x04u:
        return bridge_enable_.load(std::memory_order_acquire);
    case 0x08u:
        return bridge_status_.load(std::memory_order_acquire);
    default:
        return 0u;
    }
}

void SiemensMp377SmiBridge::Write(uint32_t pa, uint32_t value) {
    switch ((pa & ~3u) & 0x0Fu) {
    case 0x04u:
        bridge_enable_.store(value, std::memory_order_release);
        break;
    case 0x08u:
        /* Boot-time wide fills write 0xFFFFFFFF across C4100000..; do not turn
           that into a synthetic SMI. Real OAL disable/ack writes the filtered
           live status value, normally zero for raw IRQ 0x83. */
        if (value == 0xFFFFFFFFu) return;

        bridge_status_.store(value & kBridgeValidStatusBits, std::memory_order_release);
        if ((value & kBridgeValidStatusBits) == 0u)
            ClearPending();
        break;
    default:
        break;
    }
}

void SiemensMp377SmiBridge::SaveState(StateWriter& w) const {
    uint32_t v = master_pending_.load(std::memory_order_acquire);
    w.Write(v);
    v = bridge_status_.load(std::memory_order_acquire);
    w.Write(v);
    v = bridge_enable_.load(std::memory_order_acquire);
    w.Write(v);
}

void SiemensMp377SmiBridge::RestoreState(StateReader& r) {
    uint32_t master = 0;
    uint32_t status = 0;
    uint32_t enable = 0xFFFFFFFFu;
    r.Read(master);
    r.Read(status);
    r.Read(enable);
    master_pending_.store(master ? 1u : 0u, std::memory_order_release);
    bridge_status_.store(status & kBridgeValidStatusBits, std::memory_order_release);
    bridge_enable_.store(enable, std::memory_order_release);
}

void SiemensMp377SmiBridge::PostRestoreState() {
    if (master_pending_.load(std::memory_order_acquire) ||
        (bridge_status_.load(std::memory_order_acquire) & kBridgeStatusBit) != 0u) {
        AssertCascade();
    } else {
        DeassertCascade();
    }
}

bool SiemensMp377SmiBridgeWindow::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::SiemensMP377;
}

void SiemensMp377SmiBridgeWindow::OnReady() { emu_.Get<PeripheralDispatcher>().Register(this); }

uint8_t SiemensMp377SmiBridgeWindow::ReadByte(uint32_t a) {
    return static_cast<uint8_t>(ReadWord(a & ~3u) >> ((a & 3u) * 8u));
}

uint16_t SiemensMp377SmiBridgeWindow::ReadHalf(uint32_t a) {
    return static_cast<uint16_t>(ReadWord(a & ~3u) >> ((a & 2u) * 8u));
}

uint32_t SiemensMp377SmiBridgeWindow::ReadWord(uint32_t a) {
    const uint32_t rel = RelativeOffset(a);
    if (!IsSupportedOffset(rel)) {
        HaltUnsupportedAccess("MP377 SMI bridge unknown register read", a, 0);
    }
    return emu_.Get<SiemensMp377SmiBridge>().Read(rel);
}

void SiemensMp377SmiBridgeWindow::WriteByte(uint32_t a, uint8_t v) {
    /* The C410 bridge address range overlaps the early B800 text-console
       ATU alias. This is a board/OAL console path, not a SM501 framebuffer
       alias. Byte/half writes there are console stores, while live SMI
       register traffic uses word accesses to the modelled bridge registers. */
    if (IsC410ConsoleAlias()) return;
    if (v == 0xFFu) return;

    uint32_t w = ReadWord(a & ~3u);
    const uint32_t shift = (a & 3u) * 8u;
    w = (w & ~(0xFFu << shift)) | (static_cast<uint32_t>(v) << shift);
    WriteWord(a & ~3u, w);
}

void SiemensMp377SmiBridgeWindow::WriteHalf(uint32_t a, uint16_t v) {
    if (IsC410ConsoleAlias()) return;
    if (v == 0xFFFFu) return;

    uint32_t w = ReadWord(a & ~3u);
    const uint32_t shift = (a & 2u) * 8u;
    w = (w & ~(0xFFFFu << shift)) | (static_cast<uint32_t>(v) << shift);
    WriteWord(a & ~3u, w);
}

void SiemensMp377SmiBridgeWindow::WriteWord(uint32_t a, uint32_t v) {
    if (v == 0xFFFFFFFFu) return;

    const uint32_t rel = RelativeOffset(a);
    if (!IsSupportedOffset(rel)) {
        HaltUnsupportedAccess("MP377 SMI bridge unknown register write", a, v);
    }
    emu_.Get<SiemensMp377SmiBridge>().Write(rel, v);
}

bool SiemensMp377SmiBridgeWindow::IsC410ConsoleAlias() const { return false; }

uint32_t SiemensMp377SmiBridgeWindow::RelativeOffset(uint32_t a) const {
    return (a - MmioBase()) & ~3u;
}

bool SiemensMp377SmiBridgeWindow::IsSupportedOffset(uint32_t rel) {
    return rel == 0x04u || rel == 0x08u;
}

uint32_t SiemensMp377SmiBridgeC410::MmioBase() const { return SmiBridgeBase(Mp377SmiBridgeWindowId::C410); }

uint32_t SiemensMp377SmiBridgeC410::MmioSize() const { return kSmiBridgeWindowBytes; }

bool SiemensMp377SmiBridgeC410::IsC410ConsoleAlias() const { return true; }

void SiemensMp377SmiBridgeC410::SaveState(StateWriter& w) {
    emu_.Get<SiemensMp377SmiBridge>().SaveState(w);
}

void SiemensMp377SmiBridgeC410::RestoreState(StateReader& r) {
    emu_.Get<SiemensMp377SmiBridge>().RestoreState(r);
}

void SiemensMp377SmiBridgeC410::PostRestore() {
    emu_.Get<SiemensMp377SmiBridge>().PostRestoreState();
}

uint32_t SiemensMp377SmiBridgeC480::MmioBase() const { return SmiBridgeBase(Mp377SmiBridgeWindowId::C480); }

uint32_t SiemensMp377SmiBridgeC480::MmioSize() const { return kSmiBridgeWindowBytes; }

REGISTER_SERVICE(SiemensMp377SmiBridgeC410);
REGISTER_SERVICE(SiemensMp377SmiBridgeC480);
REGISTER_SERVICE(SiemensMp377SmiBridge);

} // namespace siemens_mp377

