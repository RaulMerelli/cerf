#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#if CERF_DEV_MODE

#include <cstdint>

namespace {

/* mctl.dll modem-detect delay spins in sub_F2399C (0xF2399C): deadline=R0,
   tick=off_F64A0C(0xF64A0C)=clock value()/10, clock obj=off_F61FFC(0xF61FFC)
   (vtable+16 value(), +36 status()), gate=*(*0xF65368+4). Dumps these to show
   if the tick is frozen and to name the value()/status() addresses to decompile. */
class MctlClockSpinProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        emu_.Get<TraceManager>().RegisterForBundle(kBundleCrc32, [this] {
            emu_.Get<TraceManager>().OnPc(0xF2399Cu, [this](const TraceContext& c) {
                const uint32_t deadline = c.regs[0];
                const uint32_t tick     = c.ReadVa32(0xF64A0Cu).value_or(0xDEADBEEFu);
                ++fires_;
                if (tick == last_tick_ && (fires_ % 200000u) != 0u) return;
                last_tick_ = tick;

                const uint32_t obj   = c.ReadVa32(0xF61FFCu).value_or(0);
                const uint32_t vt    = obj ? c.ReadVa32(obj).value_or(0) : 0;
                const uint32_t m16   = vt  ? c.ReadVa32(vt + 16u).value_or(0) : 0;
                const uint32_t m36   = vt  ? c.ReadVa32(vt + 36u).value_or(0) : 0;
                const uint32_t gate_obj = c.ReadVa32(0xF65368u).value_or(0);
                const uint32_t gate     = gate_obj
                    ? c.ReadVa32(gate_obj + 4u).value_or(0xDEADBEEFu) : 0xDEADBEEFu;

                LOG(Trace, "[MCTLCLK] fires=%u deadline=0x%08X tick=0x%08X "
                    "gate=0x%08X obj=0x%08X vt=0x%08X value()=0x%08X "
                    "status()=0x%08X\n", fires_, deadline, tick, gate, obj,
                    vt, m16, m36);
            });
        });
    }

private:
    uint32_t fires_     = 0;
    uint32_t last_tick_ = 0xFFFFFFFFu;
};

}  /* namespace */

REGISTER_SERVICE(MctlClockSpinProbe);

#endif  /* CERF_DEV_MODE */
