#include "cerf_virt_cursor.h"
#include "cerf_virt_cursor_regs.h"
#include "cerf_virt_addr_map.h"

#include "../peripheral_dispatcher.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/log.h"
#include "../../jit/arm/arm_mmu.h"
#include "../../state/state_stream.h"

#include <cstring>

REGISTER_SERVICE(CerfVirtCursor);

bool CerfVirtCursor::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void CerfVirtCursor::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t CerfVirtCursor::MmioBase() const { return CerfVirt::kCursorBase; }
uint32_t CerfVirtCursor::MmioSize() const { return CerfVirt::kCursorSize; }

uint32_t CerfVirtCursor::ReadWord(uint32_t addr) {
    if (addr - MmioBase() == CerfVirt::kCurDescVa) return desc_va_.load();
    return 0u;
}

/* Copy the descriptor out of guest memory in the issuing (gwes) context; it may
   straddle a page, so resolve per page through the live MMU - same as gpe_cmd. */
bool CerfVirtCursor::ReadBlob(uint32_t va, void* out, uint32_t total) {
    ArmMmu& mmu = emu_.Get<ArmMmu>();
    uint8_t* d = reinterpret_cast<uint8_t*>(out);
    uint32_t done = 0;
    while (done < total) {
        uint8_t* p = mmu.PeekVaToHost(va + done);
        if (!p) return false;
        const uint32_t page_left = 0x1000u - ((va + done) & 0x0FFFu);
        const uint32_t n = (total - done) < page_left ? (total - done) : page_left;
        std::memcpy(d + done, p, n);
        done += n;
    }
    return true;
}

void CerfVirtCursor::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off == CerfVirt::kCurDescVa) { desc_va_.store(value); return; }
    if (off != CerfVirt::kCurKick) return;

    CerfVirt::CerfCursorDescriptor d;
    if (!ReadBlob(desc_va_.load(), &d, (uint32_t)sizeof(d))) {
        LOG(Periph, "[CerfVirtCursor] descriptor VA 0x%08X unreadable\n", desc_va_.load());
        return;
    }

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
    w.Write<uint32_t>(desc_va_.load());
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
    r.Read(v); desc_va_.store(v);
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
