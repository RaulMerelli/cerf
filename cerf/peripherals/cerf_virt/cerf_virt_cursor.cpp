#include "cerf_virt_cursor.h"
#include "cerf_virt_cursor_regs.h"
#include "cerf_virt_addr_map.h"

#include "../peripheral_dispatcher.h"
#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../jit/guest_engine.h"
#include "../../state/state_stream.h"

#include <cstring>

REGISTER_SERVICE(CerfVirtCursor);

bool CerfVirtCursor::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void CerfVirtCursor::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
    const uint32_t pa = emu_.Get<BoardContext>().GuestAdditionsWindowBase() +
                        CerfVirt::kCurStageOffset;
    EmulatedMemory& mem = emu_.Get<EmulatedMemory>();
    mem.AddRegion(pa, CerfVirt::kCurStageSize, PAGE_READWRITE);
    stage_ = mem.TryTranslate(pa);
    if (!stage_) {
        LOG(Cerf, "[CerfVirtCursor] stage region at PA 0x%08X is not backed\n", pa);
        CerfFatalExit();
    }
    emu_.Get<GuestEngine>().SetDmaRegion(pa, CerfVirt::kCurStageSize);
}

uint32_t CerfVirtCursor::MmioBase() const {
    return emu_.Get<BoardContext>().GuestAdditionsWindowBase() + CerfVirt::kCursorOffset;
}
uint32_t CerfVirtCursor::MmioSize() const { return CerfVirt::kCursorSize; }

uint32_t CerfVirtCursor::ReadWord(uint32_t) {
    return 0u;
}

void CerfVirtCursor::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    (void)value;
    if (off != CerfVirt::kCurKick) return;

    const CerfVirt::CerfCursorDescriptor& d =
        *reinterpret_cast<const CerfVirt::CerfCursorDescriptor*>(stage_);

    GuestCursorShape s;
    s.visible = d.visible != 0;
    s.cx = d.cx; s.cy = d.cy; s.xhot = d.xhot; s.yhot = d.yhot; s.stride = d.stride;
    if (s.visible) {
        const uint32_t need = d.stride * d.cy * 2u;
        if (d.cy > CerfVirt::kCursorMaxDim || d.stride > CerfVirt::kCursorMaxStride ||
            need > CerfVirt::kCursorBitsBytes || need == 0u) {
            LOG(Periph, "[CerfVirtCursor] cursor %ux%u stride %u rejected\n",
                d.cx, d.cy, d.stride);
            return;
        }
        s.bits.assign(d.bits, d.bits + need);
    }

    {
        std::lock_guard<std::mutex> lk(shape_mutex_);
        shape_ = std::move(s);
        has_shape_ = true;
    }
    seq_.fetch_add(1u);
}

void CerfVirtCursor::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(shape_mutex_);
    w.Write<uint32_t>(seq_.load());
    w.Write<uint8_t>(has_shape_ ? 1u : 0u);
    w.Write<uint8_t>(shape_.visible ? 1u : 0u);
    w.Write(shape_.cx);
    w.Write(shape_.cy);
    w.Write(shape_.xhot);
    w.Write(shape_.yhot);
    w.Write(shape_.stride);
    w.Write<uint64_t>(shape_.bits.size());
    if (!shape_.bits.empty()) w.WriteBytes(shape_.bits.data(), shape_.bits.size());
}

void CerfVirtCursor::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(shape_mutex_);
    uint32_t v;
    r.Read(v); seq_.store(v);
    uint8_t b;
    r.Read(b); has_shape_ = (b != 0);
    r.Read(b); shape_.visible = (b != 0);
    r.Read(shape_.cx);
    r.Read(shape_.cy);
    r.Read(shape_.xhot);
    r.Read(shape_.yhot);
    r.Read(shape_.stride);
    uint64_t n = 0;
    r.Read(n);
    shape_.bits.resize(static_cast<size_t>(n));
    if (n) r.ReadBytes(shape_.bits.data(), static_cast<size_t>(n));
}

bool CerfVirtCursor::GetShape(GuestCursorShape& out) {
    std::lock_guard<std::mutex> lk(shape_mutex_);
    if (!has_shape_) return false;
    out = shape_;
    return true;
}
