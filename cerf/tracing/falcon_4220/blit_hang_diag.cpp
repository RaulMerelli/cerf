#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm/arm_mmu.h"
#include "bundle.h"

#include <atomic>

#if CERF_DEV_MODE

namespace {

/* DDI.DLL maps only in the GWES process, so blit-core sub_184B308 @ 0x184B308
   is unambiguous (slot logged). Single epilogue 0x184B9E0; pixel copy
   sub_184B9FC @ 0x184B9FC. a11(ROP) is stack arg [SP+0x18]; the recursive
   format-convert "second blit" passes ROP=0xCCCC. */
class Falcon4220BlitHangDiag : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            tm.OnPc(0x184B308u, [](const TraceContext& c) {
                static std::atomic<uint64_t> seq{0};
                const uint64_t s = seq.fetch_add(1, std::memory_order_relaxed);
                const uint32_t a1  = c.regs[0];   /* real dst SURFOBJ. */
                const uint32_t rop = c.ReadVa32(c.regs[13] + 0x18u).value_or(0);
                const uint32_t gpe = c.ReadVa32(a1 + 8u).value_or(0);
                const uint32_t pb  = gpe ? c.ReadVa32(gpe + 0x14u).value_or(0) : 0;
                LOG(Trace, "[BLT] ENTER seq=%llu a1=0x%08X gpe=0x%08X "
                           "pbits=0x%08X rop=0x%08X\n",
                    static_cast<unsigned long long>(s), a1, gpe, pb, rop);
                static std::atomic<bool> srcdone{false};
                const uint32_t a2 = c.regs[1];   /* source SURFOBJ. */
                if ((rop & 0xFFFFu) == 0xCCCCu && a2 &&
                        !srcdone.exchange(true)) {
                    const uint32_t sg = c.ReadVa32(a2 + 8u).value_or(0);
                    const uint32_t sp = sg ? c.ReadVa32(sg + 0x14u).value_or(0) : 0;
                    auto P = [&](uint32_t o) {
                        return sp ? c.ReadVa32(sp + o).value_or(0xDEADBEEFu) : 0u;
                    };
                    LOG(Trace, "[BLT] SRC a2=0x%08X sgpe=0x%08X spbits=0x%08X "
                        "px=%08X %08X %08X %08X\n",
                        a2, sg, sp, P(0), P(4), P(8), P(12));
                }
            });
            tm.OnPc(0x184B9E0u, [](const TraceContext&) {
                static std::atomic<uint64_t> ret{0};
                const uint64_t r = ret.fetch_add(1, std::memory_order_relaxed);
                LOG(Trace, "[BLT] RET n=%llu\n",
                    static_cast<unsigned long long>(r));
            });
            tm.OnPc(0x184B9FCu, [](const TraceContext& c) {
                static std::atomic<bool> done{false};
                if (done.exchange(true)) return;   /* one-shot surface dump. */
                const uint32_t s = c.regs[0];      /* dst surface object. */
                auto R = [&](uint32_t o) {
                    return c.ReadVa32(s + o).value_or(0xDEADBEEFu);
                };
                LOG(Trace, "[BLT] SURF 0x%08X: %08X %08X %08X %08X %08X %08X "
                    "%08X %08X %08X %08X %08X %08X\n", s,
                    R(0x00), R(0x04), R(0x08), R(0x0C), R(0x10), R(0x14),
                    R(0x18), R(0x1C), R(0x20), R(0x24), R(0x28), R(0x2C));
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(Falcon4220BlitHangDiag);

#endif  /* CERF_DEV_MODE */
