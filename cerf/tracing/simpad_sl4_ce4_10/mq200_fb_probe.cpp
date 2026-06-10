#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/mediaq_mq200/mediaq_mq200.h"
#include "bundle.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

#if CERF_DEV_MODE

namespace {

/* Dump the visible MQ200 window (16-bpp RGB565) to a 24-bpp bottom-up BMP, to
   visually confirm the guest desktop renders (not just that the FB is nonzero). */
void DumpWindowBmp(const char* path, const uint8_t* fb, uint32_t off,
                   uint32_t w, uint32_t h, uint32_t stride) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return;
    const uint32_t rowbytes = w * 3u;
    const uint32_t pad = (4u - (rowbytes & 3u)) & 3u;
    const uint32_t imgsize = (rowbytes + pad) * h;
    uint8_t hdr[54] = {};
    hdr[0] = 'B'; hdr[1] = 'M';
    const uint32_t filesize = 54u + imgsize, dataoff = 54u, infosize = 40u;
    const uint16_t planes = 1u, bpp = 24u;
    std::memcpy(hdr + 2, &filesize, 4);
    std::memcpy(hdr + 10, &dataoff, 4);
    std::memcpy(hdr + 14, &infosize, 4);
    std::memcpy(hdr + 18, &w, 4);
    std::memcpy(hdr + 22, &h, 4);
    std::memcpy(hdr + 26, &planes, 2);
    std::memcpy(hdr + 28, &bpp, 2);
    std::memcpy(hdr + 34, &imgsize, 4);
    f.write(reinterpret_cast<char*>(hdr), 54);
    std::vector<uint8_t> row(rowbytes + pad, 0u);
    for (int y = static_cast<int>(h) - 1; y >= 0; --y) {
        const uint8_t* line = fb + off + static_cast<uint32_t>(y) * stride;
        for (uint32_t x = 0; x < w; ++x) {
            uint16_t px;
            std::memcpy(&px, line + x * 2u, 2);
            row[x * 3u + 0] = static_cast<uint8_t>((px & 0x1Fu) << 3);          /* B */
            row[x * 3u + 1] = static_cast<uint8_t>(((px >> 5) & 0x3Fu) << 2);   /* G */
            row[x * 3u + 2] = static_cast<uint8_t>(((px >> 11) & 0x1Fu) << 3);  /* R */
        }
        f.write(reinterpret_cast<char*>(row.data()), rowbytes + pad);
    }
}

/* One-shot: late in boot, scan the MQ200 visible window and report whether GWES
   actually drew pixels, then dump the window to a BMP for visual confirmation. */
class SimpadSl4Mq200FbProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSimpadSl4Ce4BundleCrc32, [&] {
            auto n    = std::make_shared<uint64_t>(0);
            auto done = std::make_shared<bool>(false);
            tm.OnRunLoopIter([n, done](const TraceContext& c) {
                if (*done) return;
                if (++*n % 1000000ull) return;
                auto& mq = c.emu.Get<MediaQMq200>();
                const uint8_t* base = mq.FbBytes();
                const uint32_t sz = mq.FbSize();
                bool any = false;
                for (uint32_t i = 0; i < sz && !any; i += 4096u) any = base[i] != 0;
                if (!any) return;
                *done = true;
                uint64_t nonzero = 0;
                for (uint32_t i = 0; i < sz; ++i) if (base[i]) ++nonzero;
                LOG(Trace, "[MQ200FB] PAINTED at iter=%llu nonzero=%llu winoff=0x%X %ux%u "
                           "bpp=%u stride=%u\n",
                    (unsigned long long)*n, (unsigned long long)nonzero,
                    mq.FbWindowOffset(), mq.GetGuestW(), mq.GetGuestH(), mq.Bpp(), mq.Stride());
                if (mq.Bpp() == 16u)
                    DumpWindowBmp("Z:/tmp/sl4_desktop.bmp", base, mq.FbWindowOffset(),
                                  mq.GetGuestW(), mq.GetGuestH(), mq.Stride());
            });
        });
    }
};

}  // namespace

REGISTER_SERVICE(SimpadSl4Mq200FbProbe);

#endif  // CERF_DEV_MODE
