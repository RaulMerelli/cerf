#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm/arm_mmu.h"
#include "bundle.h"

#include <atomic>
#include <memory>

#if CERF_DEV_MODE

namespace {

/* wavedev runtime VA is uncertain (IDA imagebase 0x1330000 vs loadVA
   0x82C4B000), so each point is hooked at BOTH candidates - hooking only one
   risks a never-firing hook misread as "path not reached". */
constexpr uint32_t kDeviceExePid = 0x08000000u;
constexpr uint32_t kLoadAdjust   = 0x8191B000u;  /* loadVA 0x82C4B000 - imagebase 0x1330000 */

class SimpadSl4WavedevInitBisect : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSimpadSl4Ce4BundleCrc32, [&] {
            Mark2(tm, 0x133432Cu, "WAV_Init_0 entry");
            Mark2(tm, 0x1331BF0u, "PDD_AudioInitialize entry");
            Mark2(tm, 0x1331890u, "PDD sub_1331890 (near end)");
            Mark2(tm, 0x133404Cu, "IST StartAddr WFSO (IST running)");
            /* PDD sub-calls in body order; last to fire = the blocking sub. */
            MarkRaw(tm, 0x1332214u, "PDD sub_1332214");
            MarkRaw(tm, 0x1333278u, "PDD sub_1333278 (DMA descr)");
            MarkRaw(tm, 0x133334Cu, "PDD sub_133334C");
            MarkRaw(tm, 0x1331FB8u, "PDD sub_1331FB8");
            MarkRaw(tm, 0x1334F38u, "PDD sub_1334F38 (TchAudLock)");
            MarkRaw(tm, 0x1332308u, "PDD sub_1332308");
            MarkRaw(tm, 0x1332FECu, "PDD sub_1332FEC (volume)");
            MarkRaw(tm, 0x1331B5Cu, "PDD sub_1331B5C (reg read)");
            /* Return-point bisect WITHIN sub_1331FB8: last reached = the
               internal call that did not return. */
            MarkRaw(tm, 0x1331FFCu, "FB8 after sub_1333278");
            MarkRaw(tm, 0x1332000u, "FB8 after sub_133334C");
            MarkRaw(tm, 0x1332010u, "FB8 after sub_1335028(Sleep)");
            MarkRaw(tm, 0x1332018u, "FB8 after sub_1332214");
            MarkRaw(tm, 0x1331A6Cu, "FB8 tail sub_1331A6C");
        });
    }

private:
    static bool IsDeviceExe(const TraceContext& c) {
        return c.emu.Get<ArmMmu>().State()->process_id == kDeviceExePid;
    }
    void MarkRaw(TraceManager& tm, uint32_t ida_va, const char* tag) {
        auto fired = std::make_shared<std::atomic<bool>>(false);
        tm.OnPcFiltered(ida_va, IsDeviceExe, [fired, tag](const TraceContext& c) {
            if (fired->exchange(true)) return;
            LOG(Trace, "[WAVBISECT] %s pc=0x%08X lr=0x%08X\n",
                tag, c.pc, c.regs[14]);
        });
    }
    void Mark2(TraceManager& tm, uint32_t ida_va, const char* tag) {
        auto raw_fired = std::make_shared<std::atomic<bool>>(false);
        tm.OnPcFiltered(ida_va, IsDeviceExe, [raw_fired, tag](const TraceContext& c) {
            if (raw_fired->exchange(true)) return;
            LOG(Trace, "[WAVBISECT] %s (raw IDA VA) pc=0x%08X lr=0x%08X\n",
                tag, c.pc, c.regs[14]);
        });
        auto adj_fired = std::make_shared<std::atomic<bool>>(false);
        tm.OnPc(ida_va + kLoadAdjust, [adj_fired, tag](const TraceContext& c) {
            if (adj_fired->exchange(true)) return;
            LOG(Trace, "[WAVBISECT] %s (loadVA-adj) pc=0x%08X lr=0x%08X\n",
                tag, c.pc, c.regs[14]);
        });
    }
};

}  // namespace

REGISTER_SERVICE(SimpadSl4WavedevInitBisect);

#endif  // CERF_DEV_MODE
