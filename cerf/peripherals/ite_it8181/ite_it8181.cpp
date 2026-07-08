#include "ite_it8181.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../host/host_window.h"
#include "../peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstring>

bool IteIt8181::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::NecMobilePro700;
}

void IteIt8181::OnReady() {
    fb_.assign(kVramSize, 0u);
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void IteIt8181::PublishScreenSizeOnWriteEdge() {
    fb_written_ = true;
    if (size_published_) return;
    size_published_ = true;
    LOG(Lcd, "IteIt8181: splash surface %ux%u %ubpp stride=%u\n",
        GuestW(), GuestH(), Bpp(), StrideBytes());
    emu_.Get<HostWindow>().OnLcdEnabled(GuestW(), GuestH());
}

uint8_t IteIt8181::ReadByte(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off < kVramSize) return fb_[off];
    HaltUnsupportedAccess("IteIt8181 ReadByte", addr, 0);
}

uint16_t IteIt8181::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off + 1u < kVramSize) { uint16_t v; std::memcpy(&v, &fb_[off], sizeof(v)); return v; }
    HaltUnsupportedAccess("IteIt8181 ReadHalf", addr, 0);
}

uint32_t IteIt8181::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off + 3u < kVramSize) { uint32_t v; std::memcpy(&v, &fb_[off], sizeof(v)); return v; }
    HaltUnsupportedAccess("IteIt8181 ReadWord", addr, 0);
}

void IteIt8181::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off = addr - MmioBase();
    if (off < kVramSize) { fb_[off] = value; PublishScreenSizeOnWriteEdge(); return; }
    HaltUnsupportedAccess("IteIt8181 WriteByte", addr, value);
}

void IteIt8181::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - MmioBase();
    if (off + 1u < kVramSize) { std::memcpy(&fb_[off], &value, sizeof(value)); PublishScreenSizeOnWriteEdge(); return; }
    HaltUnsupportedAccess("IteIt8181 WriteHalf", addr, value);
}

void IteIt8181::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off + 3u < kVramSize) { std::memcpy(&fb_[off], &value, sizeof(value)); PublishScreenSizeOnWriteEdge(); return; }
    HaltUnsupportedAccess("IteIt8181 WriteWord", addr, value);
}

void IteIt8181::SaveState(StateWriter& w) {
    w.Write<uint64_t>(fb_.size());
    if (!fb_.empty()) w.WriteBytes(fb_.data(), fb_.size());
    w.Write<uint8_t>(fb_written_ ? 1u : 0u);
    w.Write<uint8_t>(size_published_ ? 1u : 0u);
}

void IteIt8181::RestoreState(StateReader& r) {
    uint64_t n = 0; r.Read(n);
    fb_.assign(static_cast<size_t>(n), 0u);
    if (n) r.ReadBytes(fb_.data(), static_cast<size_t>(n));
    uint8_t b = 0;
    r.Read(b); fb_written_     = (b != 0);
    r.Read(b); size_published_ = (b != 0);
}

REGISTER_SERVICE(IteIt8181);
