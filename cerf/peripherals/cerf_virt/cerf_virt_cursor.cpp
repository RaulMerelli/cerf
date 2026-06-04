#include "cerf_virt_cursor.h"
#include "cerf_virt_cursor_regs.h"
#include "cerf_virt_addr_map.h"

#include "../peripheral_dispatcher.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/log.h"
#include "../../jit/arm_mmu.h"

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
   straddle a page, so resolve per page through the live MMU — same as gpe_cmd. */
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

bool CerfVirtCursor::GetShape(GuestCursorShape& out) {
    std::lock_guard<std::mutex> lk(shape_mutex_);
    if (!has_shape_) return false;
    out = shape_;
    return true;
}
