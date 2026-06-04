#include "cerf_virt_resize.h"
#include "cerf_virt_resize_regs.h"
#include "cerf_virt_addr_map.h"

#include "../peripheral_dispatcher.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../host/host_window.h"

REGISTER_SERVICE(CerfVirtResize);

bool CerfVirtResize::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void CerfVirtResize::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t CerfVirtResize::MmioBase() const { return CerfVirt::kResizeBase; }
uint32_t CerfVirtResize::MmioSize() const { return CerfVirt::kResizeSize; }

uint32_t CerfVirtResize::ReadWord(uint32_t addr) {
    switch (addr - MmioBase()) {
        case CerfVirt::kRszWantW:      return want_w_.load();
        case CerfVirt::kRszWantH:      return want_h_.load();
        case CerfVirt::kRszWantBpp:    return want_bpp_.load();
        case CerfVirt::kRszWantGen:    return want_gen_.load();
        case CerfVirt::kRszAppliedW:   return applied_w_.load();
        case CerfVirt::kRszAppliedH:   return applied_h_.load();
        case CerfVirt::kRszAppliedGen: return applied_gen_.load();
        default:                       return 0u;
    }
}

void CerfVirtResize::WriteWord(uint32_t addr, uint32_t value) {
    switch (addr - MmioBase()) {
        case CerfVirt::kRszAppliedW:   applied_w_.store(value); break;
        case CerfVirt::kRszAppliedH:   applied_h_.store(value); break;
        case CerfVirt::kRszAppliedGen:
            applied_gen_.store(value);
            /* Re-mode landed on the guest. Marshal to the UI thread: the
               renderer/canvas update touches a window-owned DIB + scrollbars. */
            emu_.Get<HostWindow>().NotifyGuestRemoded(applied_w_.load(),
                                                      applied_h_.load());
            break;
        default: break;
    }
}

void CerfVirtResize::RequestResize(uint32_t w, uint32_t h, uint32_t bpp) {
    want_w_.store(w);
    want_h_.store(h);
    want_bpp_.store(bpp);
    want_gen_.fetch_add(1u);
}
