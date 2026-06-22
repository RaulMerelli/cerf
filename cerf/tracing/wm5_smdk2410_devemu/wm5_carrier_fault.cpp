#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm/arm_mmu.h"
#include "../../jit/arm/arm_cpu.h"
#include "wm5_bundle.h"

/* Hook the data-abort VECTOR, not the faulting memcpy (sub_800B3660): the memcpy
   is t=0-compiled so OnPc on it never fires, but at the vector R0..R12 are still
   the faulting context's, so the memcpy dest/src/count survive there. */

namespace {

class TraceWm5CarrierFault : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&tm] {
            tm.OnPcFiltered(
                0xFFFF0010u,
                [](const TraceContext& c) {
                    /* At the abort vector regs[] are the faulting context: R0 =
                       memcpy dest = the BVA. Filter on the slot-3 base page. */
                    return c.regs[0] >= 0x06000000u && c.regs[0] < 0x06001000u;
                },
                [](const TraceContext& c) {
                    LOG(Trace, "[CARRIERFAULT] memcpy dest(R0)=0x%08X src(R1)=0x%08X "
                               "count(R2)=0x%08X caller(R14)=0x%08X CPSR=0x%08X\n"
                               "[CARRIERFAULT]   R3=0x%08X R4=0x%08X R5=0x%08X "
                               "R6=0x%08X R7=0x%08X R12=0x%08X\n",
                        c.regs[0], c.regs[1], c.regs[2], c.regs[14], c.cpsr,
                        c.regs[3], c.regs[4], c.regs[5], c.regs[6], c.regs[7],
                        c.regs[12]);
                    /* The source identifies WHICH bytes are being copied: a carrier
                       section in the victim footprint vs. something else. */
                    for (uint32_t k = 0; k < 4; ++k) {
                        auto w = c.ReadVa32(c.regs[1] + k * 4u);
                        LOG(Trace, "[CARRIERFAULT]   src[+0x%X]=0x%08X\n",
                            k * 4u, w ? *w : 0xDEADBEEFu);
                    }
                    /* Slot context: the FCSE process_id (which process's slot is
                       active) and the slot-base global the loader OR-folds with -
                       0x06000000 = slot-3 base means dest = slot_base | offset0. */
                    auto& mmu = c.emu.Get<ArmMmu>();
                    const uint32_t pid = mmu.State()->process_id;
                    auto slot = c.ReadVa32(0x814C620Cu);
                    LOG(Trace, "[CARRIERFAULT]   process_id=0x%08X slotBase[0x814C620C]=0x%08X\n",
                        pid, slot ? *slot : 0xDEADBEEFu);
                    /* Full page-table walk (device.exe tables are live at the abort)
                       to read the memcpy caller's code (UART RA=0x36C436C0) where the
                       fast-path ReadVa32 returned DEADBEEF - so it can be byte-matched
                       to a module's .text in IDA. */
                    if (uint8_t* h = mmu.PeekVaToHost(0x36C436A0u)) {
                        const uint32_t* w = reinterpret_cast<const uint32_t*>(h);
                        for (uint32_t k = 0; k < 8; ++k)
                            LOG(Trace, "[CARRIERFAULT]   caller[0x36C436A0+0x%X]=0x%08X\n",
                                k * 4u, w[k]);
                    } else {
                        LOG(Trace, "[CARRIERFAULT]   caller code 0x36C436A0 walk FAILED\n");
                    }
                    /* The memcpy's code page is paged-out, but the faulting USER
                       stack is mapped data. Walk it: return addresses on it name
                       the call chain (range-match to modules) even when the code
                       pages aren't readable. RA came from USR mode (user-VA). */
                    auto& cpu = c.emu.Get<ArmCpu>();
                    const uint32_t usp = *cpu.GetUserModeRegisterAddress(13);
                    const uint32_t ulr = *cpu.GetUserModeRegisterAddress(14);
                    LOG(Trace, "[CARRIERFAULT]   userSP=0x%08X userLR=0x%08X\n", usp, ulr);
                    for (uint32_t k = 0; k < 24; ++k) {
                        auto w = c.ReadVa32(usp + k * 4u);
                        if (w) LOG(Trace, "[CARRIERFAULT]   uStk[+0x%X]=0x%08X\n",
                                   k * 4u, *w);
                    }
                    /* device.exe process heap handle = coredll global at user-VA
                       0x1FFFD20 (FCSE-folds to slot 3); free-list head at +0x48. */
                    auto hh = c.ReadVa32(0x1FFFD20u);
                    const uint32_t heap = hh ? *hh : 0u;
                    LOG(Trace, "[CARRIERFAULT]   heapHandle[0x1FFFD20]=0x%08X\n", heap);
                    if (heap) {
                        for (uint32_t off = 0; off <= 0x60u; off += 4u) {
                            auto w = c.ReadVa32(heap + off);
                            LOG(Trace, "[CARRIERFAULT]   heap[+0x%02X]=0x%08X\n",
                                off, w ? *w : 0xDEADBEEFu);
                        }
                    }
                    /* Walk region 0x064A0000's item chain from pitFree by item.size
                       (heap.h: busy>0, free<0, end==0); a bad size sends the walk
                       out of bounds. Find the clobbered item. */
                    auto pf = c.ReadVa32(0x064A0000u);
                    uint32_t it = pf ? *pf : 0u;
                    for (uint32_t n = 0; n < 24 && it >= 0x06000000u && it < 0x06800000u; ++n) {
                        auto sz = c.ReadVa32(it);
                        auto u1 = c.ReadVa32(it + 4u);
                        const int32_t s = sz ? (int32_t)*sz : 0;
                        LOG(Trace, "[CARRIERFAULT]   item@0x%08X size=%d (0x%08X) u=0x%08X\n",
                            it, s, sz ? *sz : 0xDEADBEEFu, u1 ? *u1 : 0xDEADBEEFu);
                        if (s == 0) break;            /* end marker */
                        const uint32_t step = (s > 0) ? (uint32_t)s : (uint32_t)(-s);
                        if (step == 0 || step > 0x200000u) break;  /* bad size */
                        it += step;
                    }
                    /* The kernel UART names the memcpy caller RA=0x36c436c0
                       (device.exe slot). Try reading it here in device.exe
                       context at the abort. */
                    for (uint32_t k = 0; k < 12; ++k) {
                        auto w = c.ReadVa32(0x36c43680u + k * 4u);
                        LOG(Trace, "[CARRIERFAULT]   caller[0x36c43680+0x%X]=0x%08X\n",
                            k * 4u, w ? *w : 0xDEADBEEFu);
                    }
                });
            /* Backup caller read: the kernel dump-symbolizer 0x800997F0 fires at
               the abort in device.exe context with R0=faulting PC (0x800B36CC).
               Read the caller bytes there if the abort-handler read fails. */
            tm.OnPcFiltered(
                0x800997F0u,
                [](const TraceContext& c) { return c.regs[0] == 0x800B36CCu; },
                [](const TraceContext& c) {
                    char b[220]; int n = 0;
                    n += std::snprintf(b + n, sizeof(b) - n,
                        "[CALLERID] symb R1(proc)=0x%08X caller 0x36c43680:",
                        c.regs[1]);
                    for (uint32_t k = 0; k < 12 && n < (int)sizeof(b) - 12; ++k) {
                        auto w = c.ReadVa32(0x36c43680u + k * 4u);
                        n += std::snprintf(b + n, sizeof(b) - n, " %08X",
                            w ? *w : 0xDEADBEEFu);
                    }
                    LOG(Trace, "%s\n", b);
                });
            /* Hook the loader-region memcpy CALL SITES (hookable) since the memcpy
               itself (sub_800B3660) is t=0-unhookable; which fires names the site. */
            static const uint32_t kLoaderMemcpySites[] = {
                0x80097268u, 0x8009745Cu, 0x800975C4u, 0x80097894u,
                0x80097CF4u, 0x8009852Cu, 0x80098F38u,
            };
            for (uint32_t site : kLoaderMemcpySites) {
                tm.OnPcFiltered(
                    site,
                    [](const TraceContext& c) {
                        const uint32_t pid = c.emu.Get<ArmMmu>().State()->process_id;
                        return pid == 0x06000000u
                            && c.regs[0] >= 0x06000000u && c.regs[0] < 0x06001000u;
                    },
                    [site](const TraceContext& c) {
                        LOG(Trace, "[LOADERMEMCPY] site=0x%08X dest(R0)=0x%08X "
                                   "src(R1)=0x%08X count(R2)=0x%08X LR=0x%08X\n",
                            site, c.regs[0], c.regs[1], c.regs[2], c.regs[14]);
                    });
            }
        });
    }
};

REGISTER_SERVICE(TraceWm5CarrierFault);

}  /* namespace */
