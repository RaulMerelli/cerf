#include <cstddef>
#include <cstring>

#include "../arm_mmu_state.h"
#include "../block_context.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../jit_block.h"
#include "../../jit_block_index.h"
#include "../../../core/log.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

/* QEMU accel/tcg/cpu-exec.c:619 tb_add_jump(), which validates the destination
   before it links and records the link so tb-maint.c:878 tb_jmp_unlink() can
   undo it. */
uint8_t* EmitChainToBlock(uint8_t* cursor, BlockContext* ctx,
                          uint32_t target_va, uint32_t slot) {
    using namespace x86;

    target_va = ArmFcseFold(target_va, ctx->fcse_pid);

    if (target_va == ctx->guest_start) {
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, chain_exit_request)));
        EmitTestRegReg(cursor, kEax, kEax);
        uint8_t* to_dispatcher = EmitJnzLabel32(cursor);
        EmitJmp32(cursor, ctx->native_start);
        FixupLabel32(to_dispatcher, cursor);
        return cursor;
    }

    constexpr uint32_t kGranuleMask = ~(kArmBlockPageBytes - 1u);
    if ((target_va & kGranuleMask) != (ctx->guest_start & kGranuleMask)) {
        return cursor;
    }

    const uint32_t expect_pa =
        ctx->phys_start + (target_va - ctx->guest_start);
    JitBlock* dest = ctx->index->FindExact(target_va);
    if (dest != nullptr &&
        (dest->native_start == nullptr || dest->phys_start != expect_pa)) {
        dest = nullptr;
    }

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
        static_cast<int32_t>(offsetof(ArmCpuState, chain_exit_request)));
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* to_dispatcher = EmitJnzLabel32(cursor);
    Emit8(cursor, 0xE9);
    uint8_t* const site = cursor;
    Emit32(cursor, 0u);

    ctx->self->chain_site[slot]       = site;
    ctx->self->chain_fallback[slot]   = cursor;
    ctx->self->chain_pending_va[slot] = target_va;
    if (dest != nullptr) {
        const uint32_t disp =
            static_cast<uint32_t>(dest->native_start - (site + 4));
        std::memcpy(site, &disp, 4);
        ctx->index->LinkChain(ctx->self, slot, dest, site, cursor);
    } else {
        const uint32_t disp = static_cast<uint32_t>(cursor - (site + 4));
        std::memcpy(site, &disp, 4);
    }
    FixupLabel32(to_dispatcher, cursor);
    return cursor;
}
