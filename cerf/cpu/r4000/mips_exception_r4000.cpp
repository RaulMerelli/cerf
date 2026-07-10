#include "../../jit/mips/mips_exception_model.h"

#include <cstdint>

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../jit/mips/mips_cpu_state.h"
#include "../../jit/mips/mips_jit.h"

namespace {

class MipsExceptionR4000 : public MipsExceptionModel {
public:
    using MipsExceptionModel::MipsExceptionModel;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && (bd->GetSoc() == SocFamily::VR4102 || bd->GetSoc() == SocFamily::VR5500);
    }

    /* mips_cpu_hw_interrupts_enabled (internal.h): IE && !EXL && !ERL. */
    bool InterruptsEnabled(const MipsCpuState& s) const override {
        if ((s.cp0_status & (1u << MipsStatusBit::kIE)) == 0u) return false;
        if (s.cp0_status & (1u << MipsStatusBit::kEXL))        return false;
        if (s.cp0_status & (1u << MipsStatusBit::kERL))        return false;
        return true;
    }

    void Enter(MipsJit* jit, uint32_t cause, bool refill_eligible) override {
        MipsCpuState& s = *jit->CpuState();

        /* Status.EXL is sampled once: it gates BOTH the refill-offset choice
           (do_interrupt EXCP_TLBL/TLBS line 1182) and the set_EPC block (line 1302),
           which both read the pre-exception value. */
        const bool exl = ((s.cp0_status >> MipsStatusBit::kEXL) & 1u) != 0u;

        uint32_t offset = MipsExcVector::kOffGeneral;            /* default 0x180 */
        if (refill_eligible && !exl) {
            offset = MipsExcVector::kOffRefill;                  /* TLB refill 0x000 */
        }

        if (!exl) {
            /* exception_resume_pc (exception.c:29): EPC = faulting PC, or branch PC
               (pc-4) in a delay slot. branch_state != kNone IS the delay-slot test -
               the branch place fn sets it before the slot, the resolve clears it only
               after. */
            const bool in_bds = (s.branch_state != MipsBranch::kNone);
            s.cp0_epc = in_bds ? (s.pc - 4u) : s.pc;
            if (in_bds) {
                s.cp0_cause |= (1u << MipsCauseBit::kBD);
            } else {
                s.cp0_cause &= ~(1u << MipsCauseBit::kBD);
            }
            s.cp0_status |= (1u << MipsStatusBit::kEXL);
        }

        /* Vector base: BEV picks the boot ROM space, else the fixed EBase. */
        const uint32_t base = ((s.cp0_status >> MipsStatusBit::kBEV) & 1u)
                                  ? MipsExcVector::kBaseBev      /* 0xBFC00200 */
                                  : MipsExcVector::kBaseNormal;  /* 0x80000000 */
        s.pc = base + offset;

        /* Cause.ExcCode (bits 6:2) is set even on a nested (EXL-already-set) take. */
        s.cp0_cause = (s.cp0_cause & ~(0x1Fu << MipsCauseBit::kExcCode)) |
                      (cause << MipsCauseBit::kExcCode);

        /* do_interrupt clears the branch-delay state unconditionally (line 1323). */
        s.branch_state = MipsBranch::kNone;
    }

    void SetMmuFaultRegs(MipsJit* jit, uint32_t va) override {
        /* raise_mmu_exception CP0 setup (tlb_helper.c:558-566), on min-page shift S:
           EntryHi/Context VPN2 = VA[31:S+1]. Context BadVPN2 field = [4+(31-S)-1 : 4]
           (VR4102 UM Fig 6-1: S=10 -> [24:4], VA>>7; R4000 S=12 -> [22:4], VA>>9). */
        MipsCpuState& s = *jit->CpuState();
        const uint32_t shift = s.min_page_shift;
        const uint32_t ctx_field = ((1u << (31u - shift)) - 1u) << 4;
        s.cp0_badvaddr = va;
        s.cp0_context  = (s.cp0_context & ~(ctx_field | 0xFu)) |
                         ((va >> (shift - 3u)) & ctx_field);
        s.cp0_entryhi  = (s.cp0_entryhi & 0xFFu) | (va & MipsVpn2Mask(shift));
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(MipsExceptionR4000, MipsExceptionModel);
