#include "cerf_virt_addr_map.h"

#include "../peripheral_base.h"
#include "../peripheral_dispatcher.h"
#include "../../boot/guest_additions_binaries.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/log.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kPageMask = 0xFFFu;

/* Serves the cerf_guest.dll body bytes to the cerf_guest_stub carrier. */
class CerfVirtGuestBody : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions;
    }

    void OnReady() override {
        LoadBody();
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return CerfVirt::kGuestBodyBase; }
    uint32_t MmioSize() const override {
        const uint32_t body =
            (static_cast<uint32_t>(body_.size()) + kPageMask) & ~kPageMask;
        return CerfVirt::kGuestBodyHdrSize + body;
    }

    FastReadFn  FastReader() override { return &FastReadThunk;  }
    FastWriteFn FastWriter() override { return &FastWriteThunk; }

private:
    void LoadBody() {
        const auto& subs = emu_.Get<DeviceConfig>().global_rom_substitutions;
        if (subs.empty()) {
            LOG(Caution, "guest body: global_rom_substitutions empty - no "
                    "cerf_guest body to serve the stub\n");
            CerfFatalExit();
        }
        const std::string path =
            emu_.Get<GuestAdditionsBinaries>().StagedPath(subs[0].second);
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.is_open()) {
            LOG(Caution, "guest body: cannot open %s - cerf_guest.dll must be "
                    "built and staged before boot\n", path.c_str());
            CerfFatalExit();
        }
        const std::streamoff sz = f.tellg();
        body_.resize(static_cast<size_t>(sz));
        f.seekg(0);
        f.read(reinterpret_cast<char*>(body_.data()), sz);

        const uint32_t need = MmioSize();
        if (need > CerfVirt::kGuestBodyMaxSize) {
            LOG(Caution, "guest body: %s needs 0x%X bytes but the body window "
                    "is only 0x%X (0x%08X..0x%08X) - raise kFramebufferMemBase "
                    "or relocate kGuestBodyBase in cerf_virt_addr_map.h\n",
                path.c_str(), need, CerfVirt::kGuestBodyMaxSize,
                CerfVirt::kGuestBodyBase,
                CerfVirt::kGuestBodyBase + CerfVirt::kGuestBodyMaxSize);
            CerfFatalExit();
        }
        LOG(GuestAdditions, "guest body: serving %s (%zu bytes) at PA 0x%08X\n",
            path.c_str(), body_.size(), CerfVirt::kGuestBodyBase);
    }

    static uint32_t FastReadThunk(void* ctx, uint32_t off, uint32_t width_bytes) {
        auto* self = static_cast<CerfVirtGuestBody*>(ctx);
        if (off < CerfVirt::kGuestBodyHdrSize)
            return (off == 0) ? static_cast<uint32_t>(self->body_.size()) : 0u;
        const uint32_t boff = off - CerfVirt::kGuestBodyHdrSize;
        uint32_t v = 0;
        for (uint32_t i = 0; i < width_bytes; ++i) {
            const size_t idx = static_cast<size_t>(boff) + i;
            if (idx < self->body_.size())
                v |= static_cast<uint32_t>(self->body_[idx]) << (i * 8);
        }
        return v;
    }

    static void FastWriteThunk(void*, uint32_t, uint32_t, uint32_t) {}

    std::vector<uint8_t> body_;
};

REGISTER_SERVICE(CerfVirtGuestBody);

}  /* namespace */
