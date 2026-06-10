#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>

#if CERF_DEV_MODE

namespace {

/* Unfiltered ON PURPOSE: the CFI-query (0x98) consumer process at slot VA
   0x2E01A8 is unknown — that's what this probe finds — so no pid filter exists.
   Dump the writer's instruction bytes to match against each module's .text
   (debugging.md "attribute a user-VA fire by instruction-byte signature"). */
class SimpadSl4CfiConsumerId : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSimpadSl4Ce4BundleCrc32, [&] {
            auto n = std::make_shared<std::atomic<uint32_t>>(0);
            tm.OnPc(0x002E01A8u, [n](const TraceContext& c) {
                if (n->fetch_add(1) >= 4) return;
                std::string words;
                char buf[16];
                for (int32_t off = -16; off <= 28; off += 4) {
                    auto w = c.ReadVa32(uint32_t(int32_t(c.pc) + off));
                    std::snprintf(buf, sizeof(buf), "%08X ",
                                  w ? *w : 0xDEADBEEFu);
                    words += buf;
                }
                LOG(Trace, "[CFIID] pc=0x%08X r0=0x%08X r1=0x%08X lr=0x%08X "
                           "words[-16..+28]: %s\n",
                    c.pc, c.regs[0], c.regs[1], c.regs[14], words.c_str());
            });
        });
    }
};

}  // namespace

REGISTER_SERVICE(SimpadSl4CfiConsumerId);

#endif  // CERF_DEV_MODE
