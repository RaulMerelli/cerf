#define NOMINMAX

#include "siemens_mp377_sm501_fb.h"
#include "siemens_mp377_sm501_internal.h"

#include "../../core/log.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/iop13xx/iop13xx_atu.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace siemens_mp377 {

namespace {

uint32_t Sm501FbAtuAliasRead(void* ctx, uint32_t off, uint32_t width) {
    auto* fb = static_cast<SiemensMp377Sm501Fb*>(ctx);
    const uint32_t a = kSm501FbBarBus + off;
    switch (width) {
        case 1: return fb->ReadByte(a);
        case 2: return fb->ReadHalf(a);
        case 4: return fb->ReadWord(a);
    }
    LOG(Caution, "MP377 ATU OUTBOUND SM501: unsupported BAR0 read width=%u off=0x%08X\n",
        width, off);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void Sm501FbAtuAliasWrite(void* ctx, uint32_t off, uint32_t value, uint32_t width) {
    auto* fb = static_cast<SiemensMp377Sm501Fb*>(ctx);
    const uint32_t a = kSm501FbBarBus + off;
    switch (width) {
        case 1: fb->WriteByte(a, static_cast<uint8_t>(value)); return;
        case 2: fb->WriteHalf(a, static_cast<uint16_t>(value)); return;
        case 4: fb->WriteWord(a, value); return;
    }
    LOG(Caution, "MP377 ATU OUTBOUND SM501: unsupported BAR0 write width=%u off=0x%08X value=0x%08X\n",
        width, off, value);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

}  // namespace

bool SiemensMp377Sm501Fb::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::SiemensMP377;
}

void SiemensMp377Sm501Fb::OnReady() {
    vram_.assign(kSm501FbBytes, 0u);
    auto& disp = emu_.Get<PeripheralDispatcher>();
    disp.Register(this);

    uint64_t atu_cpu_base = 0;
    uint32_t atu_window = 0xFFFFFFFFu;
    if (emu_.Get<Iop13xxAtuState>().PciMemBusToCpuPhys(
            kSm501FbBarBus, kSm501FbBytes, atu_cpu_base, &atu_window, false)) {
        disp.RegisterAlias(this, atu_cpu_base, kSm501FbBytes,
                           &Sm501FbAtuAliasRead,
                           &Sm501FbAtuAliasWrite,
                           this);
    } else {
        LOG(Caution,
            "MP377 ATU OUTBOUND SM501: BAR0-fb pci=0x%08X..0x%08X translation unavailable; high-PA alias NOT registered\n",
            kSm501FbBarBus, kSm501FbBarBus + kSm501FbBytes);
    }
}

uint32_t SiemensMp377Sm501Fb::MmioBase() const { return kSm501FbBarPa; }

uint32_t SiemensMp377Sm501Fb::MmioSize() const { return kSm501FbBytes; }

uint8_t  SiemensMp377Sm501Fb::ReadByte(uint32_t a) { return vram_[CpuVramOffset(a)]; }

uint16_t SiemensMp377Sm501Fb::ReadHalf(uint32_t a) {
    const size_t i = CpuVramOffset(a);
    return static_cast<uint16_t>(vram_[i] | (vram_[i + 1] << 8));
}

uint32_t SiemensMp377Sm501Fb::ReadWord(uint32_t a) {
    const size_t i = CpuVramOffset(a);
    return static_cast<uint32_t>(vram_[i] | (vram_[i + 1] << 8) |
                                 (vram_[i + 2] << 16) | (vram_[i + 3] << 24));
}

void SiemensMp377Sm501Fb::WriteByte(uint32_t a, uint8_t v) {
    const size_t i = CpuVramOffset(a);
    vram_[i] = v;
    NoteWrite(static_cast<uint32_t>(i));
}

void SiemensMp377Sm501Fb::WriteHalf(uint32_t a, uint16_t v) {
    const size_t i = CpuVramOffset(a);
    vram_[i] = static_cast<uint8_t>(v);
    vram_[i + 1] = static_cast<uint8_t>(v >> 8);
    NoteWrite(static_cast<uint32_t>(i));
}

void SiemensMp377Sm501Fb::WriteWord(uint32_t a, uint32_t v) {
    const size_t i = CpuVramOffset(a);
    vram_[i]     = static_cast<uint8_t>(v);
    vram_[i + 1] = static_cast<uint8_t>(v >> 8);
    vram_[i + 2] = static_cast<uint8_t>(v >> 16);
    vram_[i + 3] = static_cast<uint8_t>(v >> 24);
    NoteWrite(static_cast<uint32_t>(i));
}

const uint8_t* SiemensMp377Sm501Fb::Vram() const { return vram_.data(); }

uint8_t* SiemensMp377Sm501Fb::MutableVramFor2d() { return vram_.data(); }

bool SiemensMp377Sm501Fb::WasWritten() const { return written_; }

bool SiemensMp377Sm501Fb::WriteVramByte(uint32_t off, uint8_t value) {
    if (off >= kSm501FbBytes) return false;
    vram_[off] = value;
    Note2dWrite(off, 1u);
    return true;
}

bool SiemensMp377Sm501Fb::WriteVramHalf(uint32_t off, uint16_t value) {
    if (off + 1u >= kSm501FbBytes) return false;
    vram_[off] = static_cast<uint8_t>(value);
    vram_[off + 1u] = static_cast<uint8_t>(value >> 8);
    Note2dWrite(off, 2u);
    return true;
}

bool SiemensMp377Sm501Fb::WriteVramWord(uint32_t off, uint32_t value) {
    if (off + 3u >= kSm501FbBytes) return false;
    vram_[off] = static_cast<uint8_t>(value);
    vram_[off + 1u] = static_cast<uint8_t>(value >> 8);
    vram_[off + 2u] = static_cast<uint8_t>(value >> 16);
    vram_[off + 3u] = static_cast<uint8_t>(value >> 24);
    Note2dWrite(off, 4u);
    return true;
}

void SiemensMp377Sm501Fb::Note2dWrite(uint32_t off, uint32_t bytes) {
    if (bytes == 0) return;
    NoteWrite(off);
    NoteWrite(off + bytes - 1u);
}

void SiemensMp377Sm501Fb::SaveState(StateWriter& w) {
    const uint64_t n = static_cast<uint64_t>(vram_.size());
    w.Write(n);
    if (n) w.WriteBytes(vram_.data(), static_cast<size_t>(n));
    w.Write(written_);
}

void SiemensMp377Sm501Fb::RestoreState(StateReader& r) {
    uint64_t n = 0;
    r.Read(n);
    if (n != static_cast<uint64_t>(kSm501FbBytes)) {
        HaltUnsupportedAccess("SM501 VRAM state size", kSm501FbBarPa, n);
    }
    vram_.resize(kSm501FbBytes);
    r.ReadBytes(vram_.data(), vram_.size());
    r.Read(written_);
}

uint32_t SiemensMp377Sm501Fb::CpuVramOffset(uint32_t a) {
    uint32_t off = 0;
    if (!Sm501FbPaToOffset(a, off)) {
        HaltUnsupportedAccess("SM501 VRAM address outside BAR0", a, 0);
    }
    return off;
}

void SiemensMp377Sm501Fb::NoteWrite(uint32_t) { written_ = true; }

bool SiemensMp377Sm501Video::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::SiemensMP377;
}

const uint8_t* SiemensMp377Sm501Video::Vram() {
    auto* fb = emu_.TryGet<SiemensMp377Sm501Fb>();
    return fb ? fb->Vram() : nullptr;
}

bool SiemensMp377Sm501Video::WasWritten() {
    auto* fb = emu_.TryGet<SiemensMp377Sm501Fb>();
    return fb && fb->WasWritten();
}

bool SiemensMp377Sm501Video::WriteVramByte(uint32_t off, uint8_t value) {
    auto* fb = emu_.TryGet<SiemensMp377Sm501Fb>();
    return fb && fb->WriteVramByte(off, value);
}

bool SiemensMp377Sm501Video::WriteVramHalf(uint32_t off, uint16_t value) {
    auto* fb = emu_.TryGet<SiemensMp377Sm501Fb>();
    return fb && fb->WriteVramHalf(off, value);
}

bool SiemensMp377Sm501Video::WriteVramWord(uint32_t off, uint32_t value) {
    auto* fb = emu_.TryGet<SiemensMp377Sm501Fb>();
    return fb && fb->WriteVramWord(off, value);
}

uint32_t SiemensMp377Sm501Video::PanelFbOffset() {
    auto* regs = emu_.TryGet<SiemensMp377Sm501Regs>();
    return regs ? regs->PanelFbOffset() : 0u;
}

uint32_t SiemensMp377Sm501Video::PanelPitchBytes() {
    auto* regs = emu_.TryGet<SiemensMp377Sm501Regs>();
    return regs ? regs->PanelPitchBytes() : 0u;
}

REGISTER_SERVICE(SiemensMp377Sm501Fb);
REGISTER_SERVICE(SiemensMp377Sm501Video);

} // namespace siemens_mp377

