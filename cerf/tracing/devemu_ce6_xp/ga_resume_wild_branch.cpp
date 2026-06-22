#include "../trace_manager.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm/arm_mmu.h"
#include "../../jit/arm/arm_mmu_state.h"

#include <atomic>

#if CERF_DEV_MODE

namespace {

/* NOT CRC-gated: GA injection may rewrite RomParserService raw bytes, so a CRC
   gate would no-op on the GA run we need. Board-gating is safe because the legit
   reset runs `start` at PA 0x30075B60 (MMU off, different JIT block VA), so this
   link-VA hook fires ONLY on the GA-resume MMU-ON wild re-entry. */
class DevEmuGaResumeWildBranch : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Smdk2410DevEmu;
    }

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();

        /* `start` entry reached MMU-on = the wild transfer landed. Registers/stack
           here are the transferring caller's state; R14 is the return addr it set
           (S13 saw 0xfffff7bf = a KData/trap addr, not the origin) so walk the
           stack to find the real source. process_id names the faulting process. */
        tm.OnPc(0x80075B60u, [](const TraceContext& c) {
            static std::atomic<int> n{0};
            if (n.fetch_add(1, std::memory_order_relaxed) >= 6) return;
            const uint32_t sp  = c.regs[13];
            auto* m = c.emu.Get<ArmMmu>().State();
            const uint32_t pid = m->process_id;
            LOG(SocReset,
                "[GAWILD] -> start VA 0x80075B60 MMU-ON (wild branch) "
                "pid=0x%08X(slot=%u) sctlr=0x%08X(M=%u,V=%u) R14=0x%08X SP=0x%08X cpsr=0x%08X "
                "R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X R12=0x%08X\n",
                pid, pid >> 25, m->control_register.word,
                m->control_register.bits.m, m->control_register.bits.v,
                c.regs[14], sp, c.cpsr,
                c.regs[0], c.regs[1], c.regs[2], c.regs[3], c.regs[12]);
            LOG(SocReset,
                "[GAWILD] stack @0x%08X: %08X %08X %08X %08X %08X %08X "
                "%08X %08X %08X %08X %08X %08X\n",
                sp,
                c.ReadVa32(sp).value_or(0),       c.ReadVa32(sp + 4u).value_or(0),
                c.ReadVa32(sp + 8u).value_or(0),  c.ReadVa32(sp + 12u).value_or(0),
                c.ReadVa32(sp + 16u).value_or(0), c.ReadVa32(sp + 20u).value_or(0),
                c.ReadVa32(sp + 24u).value_or(0), c.ReadVa32(sp + 28u).value_or(0),
                c.ReadVa32(sp + 32u).value_or(0), c.ReadVa32(sp + 36u).value_or(0),
                c.ReadVa32(sp + 40u).value_or(0), c.ReadVa32(sp + 44u).value_or(0));
        });

        /* The GPFCON identity write at start+0x2c that actually faults (MMU on, the
           0x56000050 physical address is unmapped in the running address space). */
        tm.OnPc(0x80075b8Cu, [](const TraceContext& c) {
            static std::atomic<int> n{0};
            if (n.fetch_add(1, std::memory_order_relaxed) >= 6) return;
            LOG(SocReset,
                "[GAWILD] @start+0x2c 0x80075b8c GPFCON write (FAULTS) "
                "R14=0x%08X SP=0x%08X cpsr=0x%08X pid=0x%08X\n",
                c.regs[14], c.regs[13], c.cpsr,
                c.emu.Get<ArmMmu>().State()->process_id);
        });

        /* Sequence the power path around the wild branch: which of OEMPowerOff
           (re-suspend) / WakeAddr (resume restore) runs just before the start re-entry. */
        tm.OnPc(0x80078C40u, [](const TraceContext& c) {
            static std::atomic<int> n{0};
            if (n.fetch_add(1, std::memory_order_relaxed) >= 8) return;
            LOG(SocReset, "[GASEQ] OEMPowerOff sub_80078C40 LR=0x%08X cpsr=0x%08X "
                "pid=0x%08X R0=0x%08X R1=0x%08X\n",
                c.regs[14], c.cpsr, c.emu.Get<ArmMmu>().State()->process_id,
                c.regs[0], c.regs[1]);
        });
        tm.OnPc(0x80078DBCu, [](const TraceContext& c) {
            static std::atomic<int> n{0};
            if (n.fetch_add(1, std::memory_order_relaxed) >= 8) return;
            LOG(SocReset, "[GASEQ] WakeAddr sub_80078DBC (resume restore) "
                "LR=0x%08X cpsr=0x%08X pid=0x%08X\n",
                c.regs[14], c.cpsr, c.emu.Get<ArmMmu>().State()->process_id);
        });

        /* OEMPowerOff's flash-stub jump (MOV PC,R6), reached at suspend and again
           on resume. Logs R6 + the instruction bytes the jump lands on under the
           live MMU, so the suspend-vs-resume diff names where the flash window maps. */
        tm.OnPc(0x80078D90u, [](const TraceContext& c) {
            static std::atomic<int> n{0};
            if (n.fetch_add(1, std::memory_order_relaxed) >= 6) return;
            const uint32_t r6 = c.regs[6];
            LOG(SocReset, "[GAJMP] OEMPowerOff MOV PC,R6: R6=0x%08X R7(flash[0])=0x%08X "
                "instr@R6=0x%08X 0x%08X flash@0x88000000=0x%08X sctlr=0x%08X "
                "cpsr=0x%08X pid=0x%08X\n",
                r6, c.regs[7],
                c.ReadVa32(r6).value_or(0xDEADBEEFu),
                c.ReadVa32(r6 + 4u).value_or(0xDEADBEEFu),
                c.ReadVa32(0x88000000u).value_or(0xDEADBEEFu),
                c.emu.Get<ArmMmu>().State()->control_register.word, c.cpsr,
                c.emu.Get<ArmMmu>().State()->process_id);
        });

        /* Does WakeAddr's return (LR=0x80078D2C) actually run the OEMPowerOff tail
           on resume, and how far before it diverges? Tail entry + first MMIO write. */
        tm.OnPc(0x80078D2Cu, [](const TraceContext& c) {
            static std::atomic<int> n{0};
            if (n.fetch_add(1, std::memory_order_relaxed) >= 6) return;
            LOG(SocReset, "[GATAIL] tail entry 0x80078D2C LR=0x%08X cpsr=0x%08X "
                "sctlr=0x%08X pid=0x%08X\n", c.regs[14], c.cpsr,
                c.emu.Get<ArmMmu>().State()->control_register.word,
                c.emu.Get<ArmMmu>().State()->process_id);
        });
        tm.OnPc(0x80078D34u, [](const TraceContext& c) {
            static std::atomic<int> n{0};
            if (n.fetch_add(1, std::memory_order_relaxed) >= 6) return;
            LOG(SocReset, "[GATAIL] first MMIO write 0x80078D34 R0=0x%08X R1=0x%08X "
                "cpsr=0x%08X\n", c.regs[0], c.regs[1], c.cpsr);
        });
    }
};

}  /* namespace */

REGISTER_SERVICE(DevEmuGaResumeWildBranch);

#endif  /* CERF_DEV_MODE */
