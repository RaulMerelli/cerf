#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "wm5_bundle.h"

#include <atomic>

namespace {

class TraceWm5StartupAudit : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&] {
            tm.OnRunLoopIter([this](const TraceContext& c) { BootHvDump (c); });
            tm.OnRunLoopIter([this](const TraceContext& c) { W32Method  (c); });
            /* Vector page dump triggered late - after scheduler entry, when
               kernel has finished setting up vectors. */
            tm.OnPc(0x8007B36Cu, [this](const TraceContext& c) { AbortVec(c); });
        });
    }

private:
    /* boot.hv first 64 bytes at PA 0x34009FBC (S3C2410 OEMAddressTable
       extended-DRAM window: VA 0x94009FBC → PA 0x34009FBC). */
    void BootHvDump(const TraceContext& c) {
        if (boot_hv_done_.exchange(true, std::memory_order_relaxed)) return;
        auto& mem = c.emu.Get<EmulatedMemory>();
        const uint32_t hv_pa = 0x34009FBCu;
        LOG(Trace, "[BOOT_HV_DUMP] first 64 bytes of boot.hv at PA 0x%08X:\n", hv_pa);
        for (uint32_t off = 0; off < 64; off += 16) {
            LOG(Trace, "[BOOT_HV_DUMP]   +0x%02X: %08X %08X %08X %08X\n",
                off,
                mem.ReadWord(hv_pa + off),
                mem.ReadWord(hv_pa + off + 4),
                mem.ReadWord(hv_pa + off + 8),
                mem.ReadWord(hv_pa + off + 12));
        }
    }

    /* Abort vector instructions at 0xFFFF000C (prefetch) /
       0xFFFF0010 (data), read via OEMAddressTable extended window. */
    void AbortVec(const TraceContext& c) {
        if (abort_vec_done_.exchange(true, std::memory_order_relaxed)) return;
        auto& mem = c.emu.Get<EmulatedMemory>();
        /* Real vec_PA_base for VA 0xFFFF0000 in this bundle is 0x314A4000
           (per abort_vec_watches L2 walk). Dump 8 vector instructions +
           handler-address pool tail (offset 0x3E0). */
        const uint32_t vec_pa = 0x314A4000u;
        LOG(Trace, "[VEC_DUMP] vector insn page @ PA 0x%08X (VA 0xFFFF0000):\n", vec_pa);
        for (uint32_t off = 0; off < 0x20; off += 4) {
            LOG(Trace, "[VEC_DUMP]   +0x%02X (%s) = 0x%08X\n", off,
                off == 0x00 ? "Reset" :
                off == 0x04 ? "Undef" :
                off == 0x08 ? "SWI  " :
                off == 0x0C ? "PFA  " :
                off == 0x10 ? "DA   " :
                off == 0x14 ? "Rsvd " :
                off == 0x18 ? "IRQ  " :
                              "FIQ  ",
                mem.ReadWord(vec_pa + off));
        }
        LOG(Trace, "[VEC_DUMP] handler-address pool tail @ PA 0x%08X:\n", vec_pa + 0x3E0);
        for (uint32_t off = 0x3E0; off < 0x400; off += 4) {
            LOG(Trace, "[VEC_DUMP]   +0x%X = 0x%08X\n", off,
                mem.ReadWord(vec_pa + off));
        }
    }

    /* Win32 method[] array at PA 0x30071990 (VA 0x80071990).
       method[146] = DisableThreadLibraryCalls handler;
       IDA's nk.exe shows 0x8009966C. */
    void W32Method(const TraceContext& c) {
        if (w32_method_done_.exchange(true, std::memory_order_relaxed)) return;
        auto& mem = c.emu.Get<EmulatedMemory>();
        const uint32_t method0_pa = 0x30071990u;
        LOG(Trace, "[W32METHOD_DUMP] Win32 method[] array at PA 0x%08X "
                   "(VA 0x80071990); method[146] expected 0x8009966C:\n",
            method0_pa);
        for (uint32_t idx = 140; idx <= 152; ++idx) {
            const uint32_t v = mem.ReadWord(method0_pa + idx * 4u);
            LOG(Trace, "[W32METHOD_DUMP]   method[%u] (PA 0x%08X) = 0x%08X%s\n",
                idx, method0_pa + idx * 4u, v,
                idx == 146 ? "  <-- DisableThreadLibraryCalls" : "");
        }
    }

    std::atomic<bool> boot_hv_done_   {false};
    std::atomic<bool> abort_vec_done_ {false};
    std::atomic<bool> w32_method_done_{false};
};

REGISTER_SERVICE(TraceWm5StartupAudit);

}  /* namespace */
