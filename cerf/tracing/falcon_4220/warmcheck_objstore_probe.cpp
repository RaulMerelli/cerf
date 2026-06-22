#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm/arm_mmu.h"
#include "../../cpu/emulated_memory.h"
#include "bundle.h"

#if CERF_DEV_MODE

namespace {

uint32_t Slot(const TraceContext& c) {
    return c.emu.Get<ArmMmu>().State()->process_id >> 25;
}

/* filesys (slot 2) sub_24600 object-store mapper. Hook 0x246E8 not 0x246E4:
   the guest's LDR R0,[R3,#4] must execute first so the FS-heap page is
   GuestTlb-hot, else ReadVa32(R3+8) returns nullopt. */
class FalconObjStoreProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kFalcon4220BundleCrc32, [&] {
            TracePredicate filesys = [](const TraceContext& c) { return Slot(c) == 2u; };
            tm.OnPcFiltered(0x246E8u, filesys, [](const TraceContext& c) {
                const uint32_t base = c.regs[3];
                auto m8 = c.ReadVa32(base + 8u);
                LOG(Trace, "[FALCON-OS] FS-heap VA=0x%08X +4(R0)=0x%08X +8=0x%08X slot=%u\n",
                    base, c.regs[0], m8 ? *m8 : 0xDEADu, Slot(c));
            });
            /* Kernel sub_800DB854 @ 0x800DB8FC (CMP R0,#0x400, after LDR R0,[R5]):
               R5 = fs_heap base, R0 = raw *(base+0); +4/+8 not yet re-init'd, so
               these are the values SDRAM carried across the reset. force_clean is
               the kernel global at 0x822B6650 (cold-boot gate). Kernel VA. */
            /* 0x800F4064: R1 = MEMORY[0xBB300000] (DOC status gating the RAM-clear).
               0x800F40A4: BL memset(R0=*(romhdr+0x18)=ulRAMFree, 0, 12) - if this
               fires, the guest clears the object-store header; R0 must == fs_heap
               base, confirming this memset is the wipe. */
            tm.OnPc(0x800F4064u, [](const TraceContext& c) {
                LOG(Trace, "[FALCON-CLR] DOC status [0xBB300000]=0x%08X\n", c.regs[1]);
            });
            tm.OnPc(0x800F40A4u, [](const TraceContext& c) {
                uint8_t* p = c.emu.Get<EmulatedMemory>().TryTranslate(0xA22BA004u);
                LOG(Trace, "[FALCON-CLR] RUN memset(ulRAMFree=0x%08X, 0, %u); PA+4 before=0x%08X\n",
                    c.regs[0], c.regs[2], p ? *reinterpret_cast<const uint32_t*>(p) : 0xDEADu);
            });
            tm.OnPc(0x800DB8FCu, [](const TraceContext& c) {
                const uint32_t base = c.regs[5];
                auto m4 = c.ReadVa32(base + 4u);
                auto m8 = c.ReadVa32(base + 8u);
                auto fc = c.ReadVa32(0x822B6650u);
                LOG(Trace, "[FALCON-KOS] fs_heap=0x%08X +0=0x%08X +4=0x%08X +8=0x%08X "
                    "force_clean=0x%08X\n", base, c.regs[0], m4 ? *m4 : 0xDEADu,
                    m8 ? *m8 : 0xDEADu, fc ? *fc : 0xDEADu);
            });
            /* TryTranslate reads PA directly, valid even when the guest MMU is
               off (unlike ReadVa32) - required to track the fs_heap across the
               MMU-off reset window. fs_heap+4 PA = 0xA22BA004 (OAT 0x80000000->
               0xA0000000). */
            tm.OnRunLoopIter([last = uint32_t(0x1u)](const TraceContext& c) mutable {
                uint8_t* p = c.emu.Get<EmulatedMemory>().TryTranslate(0xA22BA004u);
                const uint32_t cur = p ? *reinterpret_cast<const uint32_t*>(p) : 0xDEADu;
                if (cur != last) {
                    last = cur;
                    LOG(Trace, "[FALCON-PHYS] PA 0xA22BA004 = 0x%08X\n", cur);
                }
            });
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(FalconObjStoreProbe);

#endif  /* CERF_DEV_MODE */
