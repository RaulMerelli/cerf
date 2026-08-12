#include "arm_block_compiler.h"

#include <cstring>

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../core/rate_probe.h"
#include "../../cpu/arm_processor_config.h"
#include "../../tracing/trace_manager.h"
#include "../x86_emit.h"
#include "../x86_emit_alu.h"
#include "arm_cpu.h"
#include "arm_decoder.h"
#include "arm_emit_services.h"
#include "arm_mmu.h"
#include "arm_mmu_probe.h"
#include "arm_mmu_state.h"
#include "arm_opcode.h"
#include "arm_page_walker.h"
#include "arm_routed_instruction.h"
#include "arm_translation_cache.h"
#include "place_fns.h"

REGISTER_SERVICE(ArmBlockCompiler);

namespace {

constexpr size_t kArmTrampolineBytes  = 64;
constexpr size_t kArmBlockReserveBytes = 128u * 1024u;

constexpr int32_t kCpsrOff =
    static_cast<int32_t>(offsetof(ArmCpuState, cpsr));
constexpr int32_t kCycleOff =
    static_cast<int32_t>(offsetof(ArmCpuState, guest_cycle_counter));
constexpr int32_t kPcOff =
    static_cast<int32_t>(offsetof(ArmCpuState, gprs) + ArmGpr::kR15 * 4u);

void EmitCycleAdvance(uint8_t*& cursor, uint32_t cycles) {
    using namespace x86;
    if (cycles >= 0x80u) {
        LOG(Caution, "ArmBlockCompiler::GenerateCode: cycle cost %u exceeds the "
                "sign-extended imm8 range\n", cycles);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    EmitAddBaseDisp32Imm8(cursor, kStateReg, kCycleOff,
                          static_cast<uint8_t>(cycles));
}

/* ARM DDI 0406C.c B1.3.3 (p. B1-1148): CPSR N[31] Z[30] C[29] V[28]. */
constexpr uint32_t kFlagN = 1u << 31;
constexpr uint32_t kFlagZ = 1u << 30;
constexpr uint32_t kFlagC = 1u << 29;
constexpr uint32_t kFlagV = 1u << 28;

/* ARM DDI 0406C.c Table A8-1 (p. A8-288): EQ Z==1, NE Z==0, CS C==1, CC C==0,
   MI N==1, PL N==0, VS V==1, VC V==0, HI "C==1 and Z==0", LS "C==0 or Z==1",
   GE N==V, LT N!=V, GT "Z==0 and N==V", LE "Z==1 or N!=V", AL Any. Returns the
   label of the jump taken when the condition is FALSE. */
uint8_t* EmitConditionGuard(uint8_t*& cursor, uint32_t cond) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, kCpsrOff);
    switch (cond) {
    case 0u: EmitTestRegImm32(cursor, kEax, kFlagZ); return EmitJzLabel32(cursor);
    case 1u: EmitTestRegImm32(cursor, kEax, kFlagZ); return EmitJnzLabel32(cursor);
    case 2u: EmitTestRegImm32(cursor, kEax, kFlagC); return EmitJzLabel32(cursor);
    case 3u: EmitTestRegImm32(cursor, kEax, kFlagC); return EmitJnzLabel32(cursor);
    case 4u: EmitTestRegImm32(cursor, kEax, kFlagN); return EmitJzLabel32(cursor);
    case 5u: EmitTestRegImm32(cursor, kEax, kFlagN); return EmitJnzLabel32(cursor);
    case 6u: EmitTestRegImm32(cursor, kEax, kFlagV); return EmitJzLabel32(cursor);
    case 7u: EmitTestRegImm32(cursor, kEax, kFlagV); return EmitJnzLabel32(cursor);
    case 8u:
    case 9u:
        EmitAndRegImm32(cursor, kEax, kFlagC | kFlagZ);
        EmitCmpRegImm32(cursor, kEax, kFlagC);
        return cond == 8u ? EmitJnzLabel32(cursor) : EmitJzLabel32(cursor);
    case 10u:
    case 11u:
        EmitMovRegReg(cursor, kEcx, kEax);
        EmitShlReg32Imm(cursor, kEcx, 3);
        EmitXorRegReg(cursor, kEcx, kEax);
        EmitTestRegImm32(cursor, kEcx, kFlagN);
        return cond == 10u ? EmitJnzLabel32(cursor) : EmitJzLabel32(cursor);
    case 12u:
    case 13u:
        EmitMovRegReg(cursor, kEcx, kEax);
        EmitShlReg32Imm(cursor, kEcx, 3);
        EmitXorRegReg(cursor, kEcx, kEax);
        EmitMovRegReg(cursor, kEdx, kEax);
        EmitShlReg32Imm(cursor, kEdx, 1);
        EmitOrReg32Reg32(cursor, kEcx, kEdx);
        EmitTestRegImm32(cursor, kEcx, kFlagN);
        return cond == 12u ? EmitJnzLabel32(cursor) : EmitJzLabel32(cursor);
    default: break;
    }
    LOG(Caution, "ArmBlockCompiler::GenerateCode: condition field %u at guard "
            "emit\n", cond);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

}

void ArmBlockCompiler::OnReady() {
    cache_     = &emu_.Get<ArmTranslationCache>();
    emit_      = &emu_.Get<ArmEmitServices>();
    cpu_       = &emu_.Get<ArmCpu>();
    cpu_state_ = cpu_->State();
    decoder_   = &emu_.Get<ArmDecoder>();
    mmu_       = &emu_.Get<ArmMmu>();
    probe_     = &emu_.Get<ArmMmuProbe>();
    walker_    = &emu_.Get<ArmPageWalker>();
    routed_    = &emu_.Get<ArmRoutedInstruction>();

    block_ctx_.emit = emit_;
    BuildTrampolines();
}

void ArmBlockCompiler::BuildTrampolines() {
    using namespace x86;

    const uint32_t self =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
    uint8_t* cursor = cache_->Arena().AllocatePermanent(kArmTrampolineBytes);

    block_ctx_.sctlr_write_target = cursor;
    EmitMovRegImm32(cursor, kEdx, self);
    EmitCall(cursor, reinterpret_cast<void*>(&ArmBlockCompiler::SctlrWriteHelper));
    EmitRet(cursor);

    block_ctx_.raise_abort_data_helper_target = cursor;
    EmitMovRegImm32(cursor, kEdx, self);
    EmitJmp32(cursor,
              reinterpret_cast<void*>(&ArmBlockCompiler::RaiseAbortDataHelper));
}

void __fastcall ArmBlockCompiler::SctlrWriteHelper(uint32_t          value,
                                                   ArmBlockCompiler* compiler) {
    ArmMmuState*   state = compiler->mmu_->State();
    const uint32_t old   = state->control_register.word;
    state->control_register.word = value;

    if (old == value) return;

    compiler->cache_->PendFlush();
}

void __fastcall ArmBlockCompiler::RaiseAbortDataHelper(
    uint32_t guest_pc, ArmBlockCompiler* compiler) {
    if (compiler->mmu_->io_pending()) {
        compiler->routed_->Complete(guest_pc);
        return;
    }
    compiler->cpu_->RaiseAbortDataException(guest_pc);
}

void __cdecl ArmBlockCompiler::TraceDispatchPcHelper(ArmBlockCompiler* compiler,
                                                     uint32_t          pc) {
    compiler->emu_.Get<TraceManager>().DispatchPc(pc, compiler->cpu_state_->gprs,
                                                  compiler->cpu_state_->cpsr.word);
}

void ArmBlockCompiler::Decode(uint32_t guest_pc, uint32_t folded_pc) {
    const uint32_t page_end =
        (folded_pc & ~(kArmBlockPageBytes - 1u)) + kArmBlockPageBytes;

    uint32_t i = 0;
    for (; i < kMaxArmInsnPerBlock && folded_pc < page_end;
         ++i, guest_pc += 4u, folded_pc += 4u) {
        DecodedInsn& insn = block_ctx_.insns[i];
        std::memset(&insn, 0, sizeof(insn));

        uint8_t* host = walker_->TranslateExecute(cpu_state_, guest_pc);
        if (host == nullptr) break;

        ArmOpcode op;
        std::memcpy(&op.word, host, sizeof(op.word));

        insn.guest_address        = guest_pc;
        insn.actual_guest_address = folded_pc;

        if (!decoder_->DecodeArm(&insn, op)) {
            insn.place_fn = &EmitRaiseUndAndReturn;
            ++i;
            break;
        }
        /* ARM DDI 0406C.c B3.15.5 (p. B3-1461): a Context synchronization
           operation is where a system-control-register write becomes
           guaranteed visible. */
        if (insn.r15_modified || insn.context_sync) {
            ++i;
            break;
        }
    }
    block_ctx_.num_insns = i;
}

size_t ArmBlockCompiler::GenerateCode(uint8_t* code, uint8_t* code_end) {
    using namespace x86;
    if (block_ctx_.num_insns == 0u) {
        LOG(Caution, "ArmBlockCompiler::GenerateCode called with num_insns == 0\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    uint8_t* const start = code;
    uint8_t*       cursor = code;
    block_ctx_.native_start = start;
    const uint32_t self =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
    TraceManager&             tm     = emu_.Get<TraceManager>();
    const ArmProcessorConfig* config = emit_->ProcessorConfig();

    for (uint32_t i = 0; i < block_ctx_.num_insns; ++i) {
        if (static_cast<size_t>(code_end - cursor) < kArmBlockReserveBytes) {
            return 0;
        }
        DecodedInsn& insn = block_ctx_.insns[i];

        if (tm.HasPcTrace(insn.guest_address)) {
            EmitMovBaseDisp32Imm32(cursor, kStateReg, kPcOff,
                                   insn.guest_address);
            EmitPush32(cursor, insn.guest_address);
            EmitPush32(cursor, self);
            EmitCall(cursor, reinterpret_cast<void*>(&TraceDispatchPcHelper));
            EmitAddRegImm32(cursor, kEsp, 8);
        }

        EmitCycleAdvance(cursor, config->CycleCostFor(insn));
        uint8_t* skip = nullptr;
        if (insn.cond != 14u) {
            skip = EmitConditionGuard(cursor, insn.cond);
        }

        cursor = insn.place_fn(cursor, &insn, &block_ctx_);

        if (skip != nullptr) {
            FixupLabel32(skip, cursor);
        }

        if (cursor > code_end) {
            LOG(Caution, "ArmBlockCompiler::GenerateCode: insn %u at 0x%08X "
                    "emitted past the slab end\n", i, insn.guest_address);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
    }

    const DecodedInsn& last = block_ctx_.insns[block_ctx_.num_insns - 1];
    if (last.cond != 14u || !last.r15_modified) {
        EmitMovBaseDisp32Imm32(cursor, kStateReg, kPcOff,
                               last.guest_address + 4u);
        EmitRet(cursor);
    }

    return static_cast<size_t>(cursor - start);
}

void* ArmBlockCompiler::Compile(uint32_t guest_pc) {
    if (cpu_state_->cpsr.bits.thumb_mode) {
        LOG(Caution, "ArmBlockCompiler::Compile: unimplemented Thumb block at "
                "guest PC 0x%08X\n", guest_pc);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    /* BranchWritePC / BXWritePC (DDI 0406C.c A2.3.2, p. A2-47): an ARM-state
       branch target always has address<1:0> == '00'. */
    if ((guest_pc & 3u) != 0u) {
        LOG(Caution, "ArmBlockCompiler::Compile: misaligned ARM-state PC 0x%08X\n",
            guest_pc);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    ArmMmuState*   mmu_state = mmu_->State();
    const uint32_t folded_pc = ArmFcseFold(guest_pc, mmu_state->process_id);

    if (walker_->TranslateExecute(cpu_state_, guest_pc) == nullptr) {
        if (mmu_->io_pending()) {
            LOG(Caution, "ArmBlockCompiler::Compile: instruction fetch at 0x%08X "
                    "resolves to peripheral PA 0x%08X\n",
                guest_pc, mmu_->io_pending_address());
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        cpu_->RaiseAbortPrefetchException(guest_pc);
        return nullptr;
    }
    const uint32_t phys_start = walker_->LastExecPa();

    JitCodeArena&  arena        = cache_->Arena();
    IsaBlockSpace& space        = cache_->Space(false);
    const uint8_t  asid         = static_cast<uint8_t>(mmu_state->contextidr & 0xFFu);
    const bool     outer_global = probe_->ExecPageGlobal(folded_pc);
    JitBlockIndex& idx          = outer_global ? space.global : space.per_asid[asid];

    JitBlock* ex = idx.FindExact(folded_pc);
    if (ex != nullptr) {
        if (ex->phys_start == phys_start) {
            space.JumpCacheInsert(folded_pc, ex->native_start, ex);
            return ex->native_start;
        }
        space.RemoveBlock(ex);
    }

#if CERF_DEV_MODE
    emu_.Get<RateProbe>().Inc(RateProbe::Counter::JitCompiles);
#endif

    Decode(guest_pc, folded_pc);
    if (block_ctx_.num_insns == 0u) {
        LOG(Caution, "ArmBlockCompiler::Compile: decoded 0 insns at 0x%08X\n",
            guest_pc);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        const size_t slab_size = arena.Remaining();
        uint8_t*     slab      = arena.Allocate(slab_size);
        if (slab_size > JitBlockIndex::OuterEntrySize()) {
            uint8_t* code = slab + JitBlockIndex::OuterEntrySize();

            JitBlock nb{};
            nb.guest_start  = folded_pc;
            nb.guest_end    = folded_pc + 4u * block_ctx_.num_insns - 1u;
            nb.phys_start   = phys_start;
            nb.native_start = code;
            JitBlock* stored = idx.PlaceOuterAt(slab, nb);
            space.IndexInsert(stored, &idx, phys_start);
            if (!outer_global) space.MarkPopulated(asid);

            const size_t code_size = GenerateCode(code, slab + slab_size);
            if (code_size != 0u) {
                arena.FreeUnusedTail(code + code_size);
                space.JumpCacheInsert(folded_pc, stored->native_start, stored);
                return stored->native_start;
            }
            space.RemoveBlock(stored);
        }
        cache_->Flush();
    }

    LOG(Caution, "ArmBlockCompiler::Compile: %u-insn block at 0x%08X does not "
            "fit a flushed %zu-byte arena\n",
        block_ctx_.num_insns, guest_pc, arena.MaxSize());
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}
