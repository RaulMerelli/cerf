#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#if CERF_DEV_MODE

namespace {

/* hpc2000 pcmcia.dll Card Services (addresses in the constants below). Hook BOTH
   the IDA VA and the loadVA(0x81C34000)-translated VA: the live execution base is
   unresolved (IDA imagebase 0xA00000), and a single wrong-base hook never fires;
   CardRequestConfiguration firing at boot confirms which base is live. */

constexpr uint32_t kIdaBase = 0x00A00000u;
constexpr uint32_t kLoadVa  = 0x81C34000u;   /* RomParser loadVA for pcmcia.dll */

constexpr uint32_t kCardSystemPowerIda = 0x00A03AA0u;
constexpr uint32_t kCardReqConfigIda   = 0x00A03648u;
constexpr uint32_t kCorWriterIda       = 0x00A020B0u;
constexpr uint32_t kCardSystemInitIda  = 0x00A02F88u;

class SimpadHpc2000PcmciaResumeTrace : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kBundleCrc32, [&] {
            HookBoth(tm, "CardSystemPower",          kCardSystemPowerIda);
            HookBoth(tm, "CardRequestConfiguration", kCardReqConfigIda);
            HookBoth(tm, "sub_A020B0(COR-write)",    kCorWriterIda);
            HookBoth(tm, "CardSystemInit",           kCardSystemInitIda);

            /* Unfiltered OnPc is safe: pcmcia.dll + ne2000.dll load only in
               device.exe (boot CardRequestConfiguration fire confirmed). DetectNE2000
               (ne2000) = PCMCIA enumerate/config entry that writes COR; sub_A08560
               (pcmcia) = IST event dispatch, r0=event code. Both at IDA VA. */
            tm.OnPc(0x01CD17F0u, [](const TraceContext& c) {
                LOG(SocReset, "[PCMCIATR] DetectNE2000 r0=0x%08X LR=0x%08X cpsr=0x%08X\n",
                    c.regs[0], c.regs[14], c.cpsr);
            });
            tm.OnPc(0x00A08560u, [](const TraceContext& c) {
                LOG(SocReset, "[PCMCIATR] IST-event code=0x%X r1=0x%X socket=0x%X LR=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[3], c.regs[14]);
            });
        });
    }

private:
    /* OnPc at the raw IDA VA and at the loadVA-translated VA; whichever fires is
       the live base. The two are distinct addresses, so no duplicate-VA halt. */
    static void HookBoth(TraceManager& tm, const char* name, uint32_t ida_va) {
        tm.OnPc(ida_va, [name](const TraceContext& c) {
            LOG(SocReset, "[PCMCIATR] %s @IDA r0=0x%08X r1=0x%08X LR=0x%08X cpsr=0x%08X\n",
                name, c.regs[0], c.regs[1], c.regs[14], c.cpsr);
        });
        const uint32_t load_va = kLoadVa + (ida_va - kIdaBase);
        tm.OnPc(load_va, [name](const TraceContext& c) {
            LOG(SocReset, "[PCMCIATR] %s @LOAD r0=0x%08X r1=0x%08X LR=0x%08X cpsr=0x%08X\n",
                name, c.regs[0], c.regs[1], c.regs[14], c.cpsr);
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(SimpadHpc2000PcmciaResumeTrace);

#endif  /* CERF_DEV_MODE */
