#include <cstddef>
#include <cstdint>
#include <intrin.h>

#include "../arm_cpu.h"
#include "../arm_jit.h"
#include "../arm_mmu.h"
#include "../arm_mmu_state.h"
#include "../cpu_state.h"
#include "../place_fns.h"
#include "../../x86_emit.h"
#include "../../../cpu/arm_processor_config.h"

namespace {

constexpr int32_t GprDisp(uint32_t n) {
    return static_cast<int32_t>(offsetof(ArmCpuState, gprs) + n * 4u);
}

}  /* namespace */

/* ARM DDI 0406C.c A8.8.58-61 (LDM family from p. A8-398), A8.8.199-202
   (STM family from p. A8-664), B9.3.5/B9.3.6/B9.3.17 (pp. B9-1986..1989,
   B9-2008): address = increment ? R[n] : R[n] - length, +4 when P == U. */
uint8_t* PlaceBlockDataTransfer(uint8_t*      cursor,
                                DecodedInsn*  d,
                                BlockContext* ctx) {
    using namespace x86;
    const ArmProcessorConfig* config = ctx->jit->ProcessorConfig();
    ArmMmu*        mmu     = ctx->jit->Mmu();
    const ArmSctlr sctlr   = mmu->State()->control_register;
    const uint32_t mmu_imm =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mmu));
    const uint32_t cpu_imm =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->jit->Cpu()));

    const uint32_t list       = d->register_list;
    const uint32_t count      = __popcnt16(d->register_list);
    const bool     load       = d->l != 0;
    const bool     wback      = d->w != 0;
    const bool     pc_in_list = (list & 0x8000u) != 0;
    const bool     exc_return = d->s != 0 && load && pc_in_list;
    const bool     user_regs  = d->s != 0 && !exc_return;
    const bool     rn_in_list = ((list >> d->rn) & 1u) != 0;

    /* Rn == 15 or an empty list is UNPREDICTABLE (A8.8.58 p. A8-398 and
       A8.8.199 p. A8-664, repeated per variant); LDM wback with Rn listed
       is UNPREDICTABLE from ARMv7 (p. A8-398); B9.3.6/B9.3.17 encode W
       as (0). */
    if (d->rn == 15u || count == 0u ||
        (load && wback && rn_in_list && config->HasCp15V7()) ||
        (user_regs && wback)) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    if (d->s != 0) {
        cursor = EmitSpsrModeGuard(cursor, d, ctx);
    }

    int32_t base_off = d->u ? 0 : -static_cast<int32_t>(4u * count);
    if (d->p == d->u) {
        base_off += 4;
    }

    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rn));
    if (base_off != 0) {
        EmitAddRegImm32(cursor, kEcx, static_cast<uint32_t>(base_off));
    }

    /* Table A3-1 (p. A3-108) + D12.3.1 (p. D12-2506): multi-word accesses
       are word-aligned or abort on ARMv7 and ARMv6 U == 1; D15.3.1
       (p. D15-2592): otherwise SCTLR.A == 0 ignores address[1:0]. */
    const bool always_fault =
        config->HasCp15V7() || (config->HasCp15V6() && sctlr.bits.u);
    const bool legacy_ignore = !always_fault && !sctlr.bits.a;
    uint8_t* align_fault_label = nullptr;
    if (legacy_ignore) {
        EmitAndRegImm32(cursor, kEcx, 0xFFFFFFFCu);
    } else {
        EmitTestRegImm32(cursor, kEcx, 3u);
        align_fault_label = EmitJnzLabel32(cursor);
    }

    const TlbAccess access = load ? TlbAccess::kRead : TlbAccess::kWrite;
    /* An LDM with Rn listed commits gprs[Rn] only after every access has
       succeeded: an abort at a later word must re-execute against the
       original base (D15.5.2, p. D15-2604). */
    const bool defer_rn = load && rn_in_list;

    uint8_t* abort_labels[17];
    int      n_abort = 0;

    int32_t rn_word_off = 0;
    int32_t cur_off     = base_off;
    bool    first       = true;
    for (uint32_t i = 0; i < 16u; ++i) {
        if (((list >> i) & 1u) == 0u) {
            continue;
        }
        if (!first) {
            EmitAddRegImm32(cursor, kEcx, 4u);
        }
        first = false;

        if (defer_rn && i == d->rn) {
            rn_word_off = cur_off;
            cur_off += 4;
            continue;
        }

        cursor = EmitTlbFastPath(cursor, ctx, access);
        EmitTestRegReg(cursor, kEax, kEax);
        abort_labels[n_abort++] = EmitJzLabel32(cursor);

        if (load) {
            /* MOV EDX, [EAX] - 8B /r (SDM Vol. 2B 4-35 MOV). */
            Emit8(cursor, 0x8B);
            EmitModRmReg(cursor, /*mod=*/0, /*rm=*/kEax, /*reg=*/kEdx);
            if (i == 15u) {
                EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(15u), kEdx);
            } else if (user_regs) {
                EmitPushReg(cursor, kEcx);
                EmitPushReg(cursor, kEdx);
                EmitPush32(cursor, i);
                EmitPush32(cursor, cpu_imm);
                EmitCall(cursor,
                    reinterpret_cast<void*>(&ArmCpu::WriteUserRegHelper));
                EmitAddRegImm32(cursor, kEsp, 12);
                EmitPopReg(cursor, kEcx);
            } else {
                EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(i), kEdx);
            }
        } else {
            if (i == 15u) {
                /* A8.8.199 (p. A8-665): MemA[address,4] = PCStoreValue(). */
                EmitMovRegImm32(cursor, kEdx,
                    d->guest_address + config->PcStoreOffset());
            } else if (user_regs) {
                EmitPushReg(cursor, kEax);
                EmitPushReg(cursor, kEcx);
                EmitPush32(cursor, i);
                EmitPush32(cursor, cpu_imm);
                EmitCall(cursor,
                    reinterpret_cast<void*>(&ArmCpu::ReadUserRegHelper));
                EmitAddRegImm32(cursor, kEsp, 8);
                EmitMovRegReg(cursor, kEdx, kEax);
                EmitPopReg(cursor, kEcx);
                EmitPopReg(cursor, kEax);
            } else {
                EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, GprDisp(i));
            }
            /* MOV [EAX], EDX - 89 /r (SDM Vol. 2B 4-35 MOV). */
            Emit8(cursor, 0x89);
            EmitModRmReg(cursor, /*mod=*/0, /*rm=*/kEax, /*reg=*/kEdx);
        }
        cur_off += 4;
    }

    if (defer_rn) {
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprDisp(d->rn));
        if (rn_word_off != 0) {
            EmitAddRegImm32(cursor, kEcx, static_cast<uint32_t>(rn_word_off));
        }
        if (legacy_ignore) {
            EmitAndRegImm32(cursor, kEcx, 0xFFFFFFFCu);
        }
        cursor = EmitTlbFastPath(cursor, ctx, TlbAccess::kRead);
        EmitTestRegReg(cursor, kEax, kEax);
        abort_labels[n_abort++] = EmitJzLabel32(cursor);
        Emit8(cursor, 0x8B);
        EmitModRmReg(cursor, /*mod=*/0, /*rm=*/kEax, /*reg=*/kEdx);
        if (user_regs) {
            EmitPushReg(cursor, kEdx);
            EmitPush32(cursor, d->rn);
            EmitPush32(cursor, cpu_imm);
            EmitCall(cursor,
                reinterpret_cast<void*>(&ArmCpu::WriteUserRegHelper));
            EmitAddRegImm32(cursor, kEsp, 12);
        } else {
            EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rn), kEdx);
        }
    }

    /* A8.8.58 (p. A8-399): wback with Rn listed leaves R[n] UNKNOWN before
       ARMv7 - the loaded value stays. */
    const bool do_wback = wback && !(load && rn_in_list);
    if (do_wback) {
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rn));
        EmitAddRegImm32(cursor, kEax,
            d->u ? 4u * count
                 : static_cast<uint32_t>(-static_cast<int32_t>(4u * count)));
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rn), kEax);
    }

    uint8_t* done_jmp = nullptr;
    if (load && pc_in_list) {
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(15u));
        if (exc_return) {
            /* B9.3.5 (p. B9-1987): wback precedes the CPSR restore. */
            EmitPushReg(cursor, kEax);
            EmitPush32(cursor, cpu_imm);
            EmitCall(cursor,
                reinterpret_cast<void*>(&ArmCpu::ExceptionReturnHelper));
            EmitAddRegImm32(cursor, kEsp, 8);
            EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(15u), kEax);
        } else {
            if (config->HasLoadToPcInterworking()) {
                cursor = EmitArmInterworkingFullEax(cursor);
            } else {
                /* BranchWritePC (A2.3.2, p. A2-47): <31:2>:'00' in ARM
                   state, <31:1>:'0' in Thumb state. */
                EmitMovRegBaseDisp32(cursor, kEcx, kStateReg,
                    static_cast<int32_t>(offsetof(ArmCpuState, cpsr)));
                EmitTestRegImm32(cursor, kEcx, 0x20u);
                uint8_t* thumb_l = EmitJnzLabel32(cursor);
                EmitAndRegImm32(cursor, kEax, 0xFFFFFFFCu);
                uint8_t* mask_done = EmitJmpLabel32(cursor);
                FixupLabel32(thumb_l, cursor);
                EmitAndRegImm32(cursor, kEax, 0xFFFFFFFEu);
                FixupLabel32(mask_done, cursor);
            }
            EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(15u), kEax);
        }
        cursor = PlaceR15ModifiedHelper(cursor, d, ctx);
    } else {
        done_jmp = EmitJmpLabel32(cursor);
    }

    const bool base_updated_abort =
        do_wback && !config->BaseRestoredAbortModel();

    /* .align_fault: ECX = start address; translate never ran, so the
       D15.5.2 writeback is unconditional here. */
    uint8_t* align_done_label = nullptr;
    if (align_fault_label != nullptr) {
        FixupLabel32(align_fault_label, cursor);
        EmitMovRegImm32(cursor, kEdx, mmu_imm);
        EmitCall(cursor, load
            ? reinterpret_cast<void*>(&ArmMmu::AlignmentFaultReadHelper)
            : reinterpret_cast<void*>(&ArmMmu::AlignmentFaultWriteHelper));
        if (base_updated_abort) {
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rn));
            EmitAddRegImm32(cursor, kEax,
                d->u ? 4u * count
                     : static_cast<uint32_t>(
                           -static_cast<int32_t>(4u * count)));
            EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rn), kEax);
            align_done_label = EmitJmpLabel32(cursor);
        }
    }

    /* .abort: */
    for (int i = 0; i < n_abort; ++i) {
        FixupLabel32(abort_labels[i], cursor);
    }
    if (base_updated_abort) {
        /* D15.5.2 (p. D15-2604) Base Updated Abort Model: a genuine abort
           (io-pending slot zero) leaves Rn updated; a routed MMIO access is
           completed whole by the trampoline instead. */
        Emit8(cursor, 0x8B);
        EmitModRmDisp32(cursor, kEax);
        Emit32(cursor, static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(mmu->IoPendingAddressPtr())));
        EmitTestRegReg(cursor, kEax, kEax);
        uint8_t* io_skip = EmitJnzLabel32(cursor);
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprDisp(d->rn));
        EmitAddRegImm32(cursor, kEax,
            d->u ? 4u * count
                 : static_cast<uint32_t>(-static_cast<int32_t>(4u * count)));
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprDisp(d->rn), kEax);
        FixupLabel32(io_skip, cursor);
    }
    if (align_done_label != nullptr) {
        FixupLabel32(align_done_label, cursor);
    }
    cursor = EmitAbortDataTail(cursor, d, ctx);

    /* .done: */
    if (done_jmp != nullptr) {
        FixupLabel32(done_jmp, cursor);
    }
    return cursor;
}
