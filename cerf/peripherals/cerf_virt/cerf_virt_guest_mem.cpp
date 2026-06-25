#include "cerf_virt_guest_mem.h"

#include "../../core/cerf_emulator.h"
#include "../../jit/guest_engine.h"

#include <cstring>

REGISTER_SERVICE(CerfVirtGuestMem);

bool CerfVirtGuestMem::ReadBlob(uint32_t va, void* dst, uint32_t n) {
    GuestEngine& engine = emu_.Get<GuestEngine>();
    uint8_t* d = static_cast<uint8_t*>(dst);
    uint32_t done = 0;
    while (done < n) {
        uint8_t* p = engine.ResolveGuestVaToHost(va + done);
        if (!p) return false;
        const uint32_t page_left = 0x1000u - ((va + done) & 0x0FFFu);
        const uint32_t k = (n - done) < page_left ? (n - done) : page_left;
        std::memcpy(d + done, p, k);
        done += k;
    }
    return true;
}

bool CerfVirtGuestMem::WriteBlob(uint32_t va, const void* src, uint32_t n) {
    GuestEngine& engine = emu_.Get<GuestEngine>();
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint32_t done = 0;
    while (done < n) {
        uint8_t* p = engine.ResolveGuestVaToHost(va + done);
        if (!p) return false;
        const uint32_t page_left = 0x1000u - ((va + done) & 0x0FFFu);
        const uint32_t k = (n - done) < page_left ? (n - done) : page_left;
        std::memcpy(p, s + done, k);
        done += k;
    }
    return true;
}
