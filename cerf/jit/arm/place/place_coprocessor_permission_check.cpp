#include <cstddef>
#include <cstdint>

#include "../../../cpu/arm_processor_config.h"
#include "../arm_jit.h"
#include "../arm_mmu_state.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit_alu.h"

/* Coproc_Accepted() - ARM DDI 0406C.c A8.7.1 (pp. A8-295/A8-296); cp10/cp11
   access controls - B1.11.1 (p. B1-1229). */
uint8_t* PlaceCoprocessorPermissionCheck(uint8_t*      cursor,
                                         DecodedInsn*  d,
                                         BlockContext* ctx) {
    using namespace x86;

    const ArmProcessorConfig* config = ctx->jit->ProcessorConfig();

    if (d->cp_num == 14u || d->cp_num == 15u) {
        const bool mcr_mrc = d->place_fn == &PlaceCoprocRegisterTransfer;
        /* CP15ISB c7,c5,4 / CP15DSB c7,c10,4 / CP15DMB c7,c10,5: WO, any
           privilege (B4.2.5 p. B4-1752; Fig D12-1 p. D12-2526 on VMSAv6;
           CP15BEN B4.1.130 p. B4-1712 - absent on Cortex-A8, SCTLR[6:3]
           RAO/SBOP per DDI 0344 Table 3-46 p. 3-46). */
        const bool pl0_barrier =
            config->HasCp15V6() && mcr_mrc && d->cp_num == 15u &&
            !d->l && d->cp_opc == 0u && d->crn == 7u &&
            ((d->crm == 5u && d->cp == 4u) ||
             (d->crm == 10u && (d->cp == 4u || d->cp == 5u)));
        /* TPIDRURW c13,c0,2 RW at PL0; TPIDRURO c13,c0,3 RO at PL0
           (Table B3-33 p. B3-1452). */
        const bool pl0_thread_id =
            config->HasCp15V7() && mcr_mrc && d->cp_num == 15u &&
            d->cp_opc == 0u && d->crn == 13u && d->crm == 0u &&
            (d->cp == 2u || (d->cp == 3u && d->l != 0u));
        /* v6 block-transfer management, unprivileged + PL1 (p. D12-2538):
           PrefetchStatus MRC c7,c12,4; StopPrefetchRange MCR c7,c12,5.
           ARM1136 implements block transfers (DDI 0211 pp. 3-101/3-102);
           ARMv7 does not (p. D12-2536). */
        const bool pl0_block_mgmt =
            config->HasCp15V6() && !config->HasCp15V7() && mcr_mrc &&
            d->cp_num == 15u && d->cp_opc == 0u && d->crn == 7u &&
            d->crm == 12u &&
            ((d->cp == 4u && d->l != 0u) || (d->cp == 5u && !d->l));
        if (pl0_barrier || pl0_thread_id || pl0_block_mgmt) {
            return cursor;
        }

        /* Other User-mode CP14/CP15 accesses are UNDEFINED, unallocated
           included (B3.15 p. B3-1448; pre-ARMv6: ARM DDI 0100I p. B3-4;
           XScale Core Dev Manual §7.1 page 77 - CP15/CP14 privileged-only,
           CDP/MRRC/MCRR undefined). */
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, cpsr)));
        EmitAndRegImm32(cursor, kEax, 0x1Fu);
        EmitCmpRegImm32(cursor, kEax, ArmMode::kUser);
        uint8_t* not_user = EmitJnzLabel32(cursor);
        cursor = EmitRaiseUndTail(cursor, d, ctx);
        FixupLabel32(not_user, cursor);
        return cursor;
    }

    /* A2.9 (p. A2-94): CP8, CP9, CP12 and CP13 are reserved; any
       coprocessor access instruction attempting to access one of these
       coprocessors is UNDEFINED. */
    if (config->HasCp15V7() &&
        (d->cp_num == 8u || d->cp_num == 9u ||
         d->cp_num == 12u || d->cp_num == 13u)) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    /* B1.9.2 (p. B1-1206): a coprocessor instruction that is not
       implemented raises the Undefined Instruction exception. */
    if ((d->cp_num == 10u || d->cp_num == 11u) &&
        !config->HasVfp() && !config->HasNeon()) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    if (!config->HasCp15V6()) {
        return cursor;
    }

    /* CPACR<2n+1:2n> (B4.1.40, p. B4-1551) per A8.7.1 Coproc_Accepted():
       00 UNDEFINED, 01 UNDEFINED at PL0, 10 UNPREDICTABLE (-> UND,
       p. Glossary-2737), 11 permitted. B1.11.1 (p. B1-1229) lists 00/01/11
       for cp10/cp11 with the same meanings. */
    EmitMovRegBaseDisp32(cursor, kEax, kMmuReg,
        static_cast<int32_t>(offsetof(ArmMmuState, coprocessor_access)));
    EmitShrReg32Imm(cursor, kEax, static_cast<uint8_t>(2u * d->cp_num));
    EmitAndRegImm32(cursor, kEax, 3u);
    EmitCmpRegImm32(cursor, kEax, 3u);
    uint8_t* pass_label = EmitJzLabel32(cursor);
    EmitCmpRegImm32(cursor, kEax, 1u);
    uint8_t* und_label_field = EmitJnzLabel32(cursor);

    /* B1.3.3 (p. B1-1148): CPSR.M[4:0] = bits[4:0]; Table B1-1
       (p. B1-1139): User = 0b10000. */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, cpsr)));
    EmitAndRegImm32(cursor, kEax, 0x1Fu);
    EmitCmpRegImm32(cursor, kEax, ArmMode::kUser);
    uint8_t* und_label_user = EmitJzLabel32(cursor);

    uint8_t* ok_label = EmitJmpLabel32(cursor);

    /* .und: */
    FixupLabel32(und_label_field, cursor);
    FixupLabel32(und_label_user, cursor);
    cursor = EmitRaiseUndTail(cursor, d, ctx);

    /* .ok: */
    FixupLabel32(ok_label, cursor);
    FixupLabel32(pass_label, cursor);
    return cursor;
}
