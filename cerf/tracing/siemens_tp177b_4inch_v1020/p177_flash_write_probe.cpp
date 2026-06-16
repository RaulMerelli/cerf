#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <cstdint>

/* P177 flash-write persistence probe (dev): hooks the guest AMD program fn
   (amdflash.c sub_83047970; r1=target, r2=data, r3=len) and on each call reads
   back the PREVIOUS target, diagnosing the formatLU LU-header write/read
   round-trip mismatch (partitionscanner.cpp:1043). */

namespace {

class TraceSiemensP177FlashWrite : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSiemensTp177bBundleCrc32, [this, &tm] {
            tm.OnPc(0x83047970u, [this](const TraceContext& c) {   /* amdflash.c program */
                if (fired_ >= 32u) return;
                ++fired_;

                const uint32_t target = c.regs[1];
                const uint32_t len    = c.regs[3];
                auto intended = c.ReadVa16(c.regs[2]);   /* first word of the data buffer */

                if (have_prev_) {
                    auto now = c.ReadVa16(prev_target_);
                    LOG(Trace, "[P177FLW] prev program target=0x%08X intended=0x%04X "
                               "now-reads=0x%04X persisted=%d\n",
                        prev_target_, prev_intended_,
                        now ? *now : 0xFFFFu,
                        (now && *now == prev_intended_) ? 1 : 0);
                }

                LOG(Trace, "[P177FLW] program target=0x%08X len=%u intended-word=0x%04X\n",
                    target, len, intended ? *intended : 0xFFFFu);

                prev_target_   = target;
                prev_intended_ = intended ? *intended : 0xFFFFu;
                have_prev_     = true;
            });
        });
    }

private:
    uint32_t fired_        = 0;
    bool     have_prev_    = false;
    uint32_t prev_target_  = 0;
    uint16_t prev_intended_ = 0;
};

REGISTER_SERVICE(TraceSiemensP177FlashWrite);

}  /* namespace */
