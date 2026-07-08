#include "mips_jit.h"

#include <cstddef>

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../tracing/trace_manager.h"
#include "../x86_emit.h"

size_t MipsJit::JitGenerateCode(uint8_t* code_location, int /* entrypoint_count */) {
    using namespace x86;
    if (block_ctx_.num_insns == 0) {
        LOG(Caution, "MipsJit::JitGenerateCode called with num_insns == 0\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    uint8_t* const start = code_location;
    constexpr int32_t kCycleOff =
        static_cast<int32_t>(offsetof(MipsCpuState, guest_cycle_counter));
    constexpr int32_t kPcOff =
        static_cast<int32_t>(offsetof(MipsCpuState, pc));
    constexpr int32_t kBranchStateOff =
        static_cast<int32_t>(offsetof(MipsCpuState, branch_state));
    const uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));

    /* Resolve once so the per-insn HasPcTrace is a single map lookup, not a
       service-locator call (mirrors ArmJit::JitGenerateCode). */
    TraceManager& tm = emu_.Get<TraceManager>();

    bool terminated = false;  /* a within-block delay-slot resolve emitted the ret */

    for (uint32_t i = 0; i < block_ctx_.num_insns; ++i) {
        MipsDecodedInsn& insn = block_ctx_.insns[i];
        /* Materialize this insn's guest PC before it runs so a fault inside any
           memory/arith helper sees the precise faulting PC in cpu_state_.pc
           (exception_resume_pc / EPC). Hot-path cost; perf-optimizable later. */
        EmitMovBaseDisp32Imm32(code_location, kStateReg, kPcOff, insn.guest_address);

        /* Trace hook (only emitted for a registered PC): cpu_state_.pc is now
           this insn, so the handler observes state as the insn is about to run.
           __cdecl(jit, pc) - push pc then jit (right-to-left). */
        if (tm.HasPcTrace(insn.guest_address)) {
            EmitPush32(code_location, insn.guest_address);
            EmitPush32(code_location, self);
            EmitCall(code_location, reinterpret_cast<void*>(&TraceDispatchPcHelper));
            EmitAddRegImm32(code_location, kEsp, 8);
        }

        EmitAddBaseDisp32Imm8(code_location, kStateReg, kCycleOff, 1);

        /* Branch-likely delay-slot nullify (QEMU decode_opc "blikely not taken"):
           MUST emit BEFORE the delay slot's place_fn so a not-taken *L skips it.
           Cross-block insn[0] gates on branch_state to keep normal entry cheap. */
        const bool inblock_likely_ds =
            (i > 0 && block_ctx_.insns[i - 1].is_branch &&
             block_ctx_.insns[i - 1].is_likely);
        const bool xblock_ds = (i == 0 && !insn.is_branch);
        if (inblock_likely_ds || xblock_ds) {
            uint8_t* j_skip_gate = nullptr;
            if (xblock_ds) {
                EmitMovRegBaseDisp32(code_location, kEax, kStateReg, kBranchStateOff);
                EmitCmpRegImm32(code_location, kEax, MipsBranch::kCondLikely);
                j_skip_gate = EmitJnzLabel(code_location);
            }
            EmitMovRegImm32(code_location, kEcx, insn.guest_address + 4u);
            EmitMovRegImm32(code_location, kEdx, self);
            EmitCall(code_location, reinterpret_cast<void*>(&NullifyLikelyHelper));
            EmitTestRegReg(code_location, kEax, kEax);
            uint8_t* j_run = EmitJzLabel(code_location);
            EmitRetn(code_location, 0);              /* nullified: pc set, exit block */
            FixupLabel(j_run, code_location);
            if (j_skip_gate) FixupLabel(j_skip_gate, code_location);
        }

        code_location = insn.place_fn(code_location, &insn, &block_ctx_);

        if (insn.is_eret) {
            /* EretHelper already set pc from EPC/ErrorEPC; suppress the
               straight-line pc override and exit. */
            EmitRetn(code_location, 0);
            terminated = true;
            break;
        }
        if (i > 0 && block_ctx_.insns[i - 1].is_branch) {
            /* Within-block delay slot (block's last insn): branch_state is pending
               (set by insns[i-1]); resolve and exit (QEMU gen_branch). */
            EmitMovRegImm32(code_location, kEcx, insn.guest_address + 4u);
            EmitMovRegImm32(code_location, kEdx, self);
            EmitCall(code_location, reinterpret_cast<void*>(&ResolveBranchHelper));
            EmitRetn(code_location, 0);
            terminated = true;
            break;  /* nothing executes after a branch's delay slot */
        }
        if (i == 0 && !insn.is_branch) {
            /* insn[0] may be a delay slot entered from a branch in the prior block
               (branch_state pending). Resolve-if-pending; a normal entry returns 0
               and the block continues (QEMU delay-slot-entry TB + gen_branch). */
            EmitMovRegImm32(code_location, kEcx, insn.guest_address + 4u);
            EmitMovRegImm32(code_location, kEdx, self);
            EmitCall(code_location, reinterpret_cast<void*>(&ResolveBranchHelper));
            EmitTestRegReg(code_location, kEax, kEax);
            uint8_t* j_continue = EmitJzLabel(code_location);
            EmitRetn(code_location, 0);
            FixupLabel(j_continue, code_location);
        }
    }

    if (!terminated) {
        const uint32_t next_pc =
            block_ctx_.insns[block_ctx_.num_insns - 1].guest_address + 4u;
        EmitMovBaseDisp32Imm32(code_location, kStateReg, kPcOff, next_pc);
        EmitRetn(code_location, 0);
    }

    return static_cast<size_t>(code_location - start);
}
