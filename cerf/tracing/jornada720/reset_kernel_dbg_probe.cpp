#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#if CERF_DEV_MODE

#include <cstdint>
#include <string>

namespace {

/* sub_800762A0(r0=char): J720 kernel per-char debug writer — accumulate into
   the OEMWriteDebugString stream. sub_800570A0: kernel banner + register dump;
   re-entry count shows whether early init is looping. */
class ResetKernelDbgProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        emu_.Get<TraceManager>().RegisterForBundle(kBundleCrc32, [this] {
            auto& tm = emu_.Get<TraceManager>();

            tm.OnPc(0x800762A0u, [this](const TraceContext& c) {
                const char ch = static_cast<char>(c.regs[0] & 0xFFu);
                if (ch == '\n') {
                    LOG(Trace, "[NKDBG] %s\n", line_.c_str());
                    line_.clear();
                } else if (ch != '\r' && line_.size() < 240) {
                    line_.push_back(ch);
                }
            });

            tm.OnPc(0x800570A0u, [this](const TraceContext& c) {
                LOG(Trace, "[NKDBG] <<< banner/fault-dump entry #%u r0=0x%08X "
                    "lr=0x%08X >>>\n", ++banner_hits_, c.regs[0], c.regs[14]);
            });
        });
    }

private:
    std::string line_;
    uint32_t    banner_hits_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(ResetKernelDbgProbe);

#endif  /* CERF_DEV_MODE */
