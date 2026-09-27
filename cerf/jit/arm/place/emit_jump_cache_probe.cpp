#include <cstddef>
#include <cstdint>

#include "../arm_mmu_state.h"
#include "../block_context.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../isa_block_space.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

/* QEMU accel/tcg/cpu-exec.c helper_lookup_tb_ptr(). */
uint8_t* EmitJumpCacheProbe(uint8_t* cursor, BlockContext* ctx) {
    using namespace x86;

    uint8_t* miss[5];
    uint32_t n = 0;

    uint8_t* to_dispatcher[2];
    cursor    = EmitDispatcherExitPoll(cursor, to_dispatcher);
    miss[n++] = to_dispatcher[0];
    miss[n++] = to_dispatcher[1];

    /* ARM DDI 0406C.c B1.3 (p. B1-1149): "T, bit[5] Thumb execution state
       bit." */
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, cpsr)));
    EmitTestRegImm32(cursor, kEax, 1u << 5);
    miss[n++] = ctx->thumb ? EmitJzLabel32(cursor) : EmitJnzLabel32(cursor);

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, gprs) + ArmGpr::kR15 * 4u));
    EmitMovRegReg(cursor, kEcx, kEax);
    EmitAndRegImm32(cursor, kEcx, 0xFE000000u);
    uint8_t* const no_fold = EmitJnzLabel32(cursor);
    EmitOrRegBaseDisp32(cursor, kEax, kMmuReg,
        static_cast<int32_t>(offsetof(ArmMmuState, fcse_fold_id)));
    FixupLabel32(no_fold, cursor);

    EmitMovRegReg(cursor, kEcx, kEax);
    EmitShrReg32Imm(cursor, kEcx, 2);
    EmitAndRegImm32(cursor, kEcx, kJumpCacheSize - 1u);
    EmitShlReg32Imm(cursor, kEcx, kJumpCacheEntryShift);
    EmitAddRegImm32(cursor, kEcx,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jump_cache)));

    EmitCmpRegBaseDisp32(cursor, kEax, kEcx,
        static_cast<int32_t>(offsetof(JumpCacheEntry, folded_va)));
    miss[n++] = EmitJnzLabel32(cursor);

    EmitMovRegBaseDisp32(cursor, kEcx, kEcx,
        static_cast<int32_t>(offsetof(JumpCacheEntry, native)));
    EmitTestRegReg(cursor, kEcx, kEcx);
    miss[n++] = EmitJzLabel32(cursor);

    /* JMP r/m32 - FF /4 (SDM Vol. 2A 3-552 JMP, Op/En M). */
    Emit8(cursor, 0xFF);
    EmitModRmReg(cursor, 3u, kEcx, 4u);

    for (uint32_t i = 0; i < n; ++i) FixupLabel32(miss[i], cursor);
    return cursor;
}
