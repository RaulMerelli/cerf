#include "arm_coproc_space_decoder.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../cpu/arm_processor_config.h"
#include "arm_opcode.h"
#include "cpu_state.h"
#include "decoded_insn.h"
#include "place_fns.h"

REGISTER_SERVICE(ArmCoprocSpaceDecoder);

bool ArmCoprocSpaceDecoder::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void ArmCoprocSpaceDecoder::OnReady() {
    processor_config_ = &emu_.Get<ArmProcessorConfig>();
}

/* DDI 0406C.c Table A5-22 (p. A5-215): op1 = insn[25:20], op = insn[4];
   00000x UNDEFINED, 11xxxx SVC, 000100 MCRR / 000101 MRRC (v5TE),
   0xxxx0 STC / 0xxxx1 LDC, 10xxxx op=0 CDP, 10xxx0/10xxx1 op=1 MCR/MRC.
   DDI 0100I A3.16.6 (p. A3-40): the coprocessor instruction extension
   space (opcode[27:23] = 11000, [21] = 0) is UNDEFINED in ARMv4 and
   non-E ARMv5; from ARMv5 E variants, bit[22] = 1 selects MCRR / MRRC.
   A1 encodings: SVC imm24 (A8-720); MCRR / MRRC Rt2 = insn[19:16],
   Rt = insn[15:12], t == 15 / t2 == 15 UNPREDICTABLE, MRRC t == t2
   UNPREDICTABLE (A8-478, A8-494); LDC imm32 = ZeroExtend(imm8:'00', 32)
   (A8-392), LDC literal wback UNPREDICTABLE (A8-394), STC n == 15 with
   wback UNPREDICTABLE (A8-662); CDP opc1 = insn[23:20] (A8-358);
   MCR / MRC opc1 = insn[23:21], opc2 = insn[7:5], MCR t == 15
   UNPREDICTABLE (A8-476), MRC t == 15 = APSR_nzcv (A8-493), which
   B3.15 (p. B3-1448) makes UNPREDICTABLE for CP14 and CP15 "except
   for the CP14 MRC to APSR_nzcv shown in CP14 debug register
   interface accesses on page C6-2124" - Table C6-1:
   MRC p14, 0, APSR_nzcv, c0, c1, 0 (DBGDSCRint). */
bool ArmCoprocSpaceDecoder::Decode(DecodedInsn* insn, ArmOpcode op) {
    const uint32_t word   = op.word;
    const uint32_t op1    = (word >> 20) & 0x3Fu;
    const uint32_t cp_num = (word >> 8) & 0xFu;

    if ((op1 & 0x30u) == 0x30u) {
        insn->r15_modified = true;
        insn->place_fn     = &PlaceSvc;
        return true;
    }
    if ((op1 & 0x3Eu) == 0x00u) {
        return false;
    }
    if ((op1 & 0x3Eu) == 0x04u) {
        if (!processor_config_->HasDsp()) {
            return false;
        }
        const uint32_t l   = op1 & 0x1u;
        const uint32_t rt  = (word >> 12) & 0xFu;
        const uint32_t rt2 = (word >> 16) & 0xFu;
        if (rt == ArmGpr::kR15 || rt2 == ArmGpr::kR15) {
            return false;
        }
        if (l != 0u && rt == rt2) {
            return false;
        }
        insn->l        = l;
        insn->crd      = rt;
        insn->rn       = rt2;
        insn->cp_num   = cp_num;
        insn->offset   = static_cast<int32_t>(word & 0xFFu);
        insn->place_fn = &PlaceCoprocExtension;
        return true;
    }
    if ((op1 & 0x20u) == 0u) {
        const uint32_t rn = (word >> 16) & 0xFu;
        const uint32_t w  = (word >> 21) & 0x1u;
        if (rn == ArmGpr::kR15 && w != 0u) {
            return false;
        }
        const int32_t imm32 = static_cast<int32_t>((word & 0xFFu) << 2);
        insn->l        = op1 & 0x1u;
        insn->p        = (word >> 24) & 0x1u;
        insn->u        = (word >> 23) & 0x1u;
        insn->n        = (word >> 22) & 0x1u;
        insn->w        = w;
        insn->rn       = rn;
        insn->crd      = (word >> 12) & 0xFu;
        insn->cp_num   = cp_num;
        insn->offset   = insn->u != 0u ? imm32 : -imm32;
        insn->place_fn = &PlaceCoprocDataTransfer;
        return true;
    }
    insn->cp_num = cp_num;
    insn->crn    = (word >> 16) & 0xFu;
    insn->crm    = word & 0xFu;
    insn->cp     = (word >> 5) & 0x7u;
    if (((word >> 4) & 0x1u) == 0u) {
        insn->cp_opc   = (word >> 20) & 0xFu;
        insn->crd      = (word >> 12) & 0xFu;
        insn->place_fn = &PlaceCoprocDataOperation;
        return true;
    }
    const uint32_t l  = op1 & 0x1u;
    const uint32_t rt = (word >> 12) & 0xFu;
    if (l == 0u && rt == ArmGpr::kR15) {
        return false;
    }
    if (l != 0u && rt == ArmGpr::kR15) {
        if (cp_num == 15u) {
            return false;
        }
        const bool dbgdscr_int = ((word >> 21) & 0x7u) == 0u &&
                                 ((word >> 16) & 0xFu) == 0u &&
                                 (word & 0xFu) == 1u &&
                                 ((word >> 5) & 0x7u) == 0u;
        if (cp_num == 14u && !dbgdscr_int) {
            return false;
        }
    }
    insn->l        = l;
    insn->cp_opc   = (word >> 21) & 0x7u;
    insn->rd       = rt;
    /* DDI 0406C.c B4.2.6 (p. B4-1753): "The deprecated CP15 c7 encoding for an
       Instruction Synchronization Barrier is an MCR instruction with <opc1> set
       to 0, <CRn> set to c7, <CRm> set to c5, and <opc2> set to 4." */
    insn->context_sync = l == 0u && cp_num == 15u && insn->cp_opc == 0u &&
                         insn->crn == 7u && insn->crm == 5u && insn->cp == 4u;
    insn->place_fn = &PlaceCoprocRegisterTransfer;
    return true;
}
