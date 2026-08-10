#include "mips_block_compiler.h"

#include <cstddef>

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../tracing/trace_manager.h"
#include "../x86_emit_alu.h"
#include "mips_cpu.h"
#include "mips_cpu_state.h"
#include "mips_emit_services.h"
#include "mips_exception_delivery.h"
#include "mips_insn_decoder.h"
#include "mips_mmu.h"
#include "mips_translation_cache.h"

REGISTER_SERVICE(MipsBlockCompiler);

void MipsBlockCompiler::OnReady() {
    cpu_state_  = emu_.Get<MipsCpu>().State();
    mmu_        = &emu_.Get<MipsMmu>();
    cache_      = &emu_.Get<MipsTranslationCache>();
    decoder_    = &emu_.Get<MipsInsnDecoder>();
    exceptions_ = &emu_.Get<MipsExceptionDelivery>();

    block_ctx_.emit     = &emu_.Get<MipsEmitServices>();
    block_ctx_.compiler = this;
}

void __cdecl MipsBlockCompiler::UnimplementedHelper(MipsBlockCompiler* /* c */,
                                                    uint32_t pc, uint32_t raw) {
    LOG(Caution, "MipsBlockCompiler: unimplemented instruction 0x%08X at guest "
            "PC 0x%08X\n", raw, pc);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void __cdecl MipsBlockCompiler::PcrelDelaySlotHelper(MipsBlockCompiler* /* c */,
                                                     uint32_t pc) {
    LOG(Caution, "MipsBlockCompiler: PC-relative MIPS16 instruction at 0x%08X "
                 "entered as a cross-block jump delay slot - its base PC (the "
                 "jump's PC, U15509EJ2V0UM Table 3-12 p67) is not modeled on "
                 "this entry\n", pc);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void __cdecl MipsBlockCompiler::TraceDispatchPcHelper(MipsBlockCompiler* c,
                                                      uint32_t pc) {
    c->emu_.Get<TraceManager>().DispatchPcMips(pc, c->cpu_state_);
}

int __fastcall MipsBlockCompiler::ResolveBranchHelper(uint32_t fallthrough,
                                                      MipsBlockCompiler* c) {
    MipsCpuState& s = *c->cpu_state_;
    if (s.branch_state == MipsBranch::kNone) {
        return 0;
    }
    if (s.branch_state == MipsBranch::kCond) {
        s.pc = s.bcond ? s.btarget : fallthrough;      /* gen_branch BC */
    } else if (s.branch_state == MipsBranch::kCondLikely) {
        s.pc = s.btarget;                              /* gen_branch BL-taken */
    } else {
        s.pc = s.btarget;                              /* gen_branch B / BR */
        /* "Only the JALX, JR, and JALR instructions change the ISA mode bit"
           (U15509EJ2V0UM 3.4.1). */
        s.isa_mode = s.btarget_isa;
    }
    s.branch_state = MipsBranch::kNone;
    return 1;
}

int __fastcall MipsBlockCompiler::NullifyLikelyHelper(uint32_t fallthrough,
                                                      MipsBlockCompiler* c) {
    MipsCpuState& s = *c->cpu_state_;
    if (s.branch_state == MipsBranch::kCondLikely && s.bcond == 0) {
        s.pc = fallthrough;            /* skip the delay slot (QEMU pc_next+4) */
        s.branch_state = MipsBranch::kNone;
        return 1;
    }
    return 0;                          /* taken, or not a likely delay slot */
}

size_t MipsBlockCompiler::GenerateCode(uint8_t* code_location) {
    using namespace x86;
    if (block_ctx_.num_insns == 0) {
        LOG(Caution, "MipsBlockCompiler::GenerateCode called with num_insns == 0\n");
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

    TraceManager& tm = emu_.Get<TraceManager>();

    bool terminated = false;

    for (uint32_t i = 0; i < block_ctx_.num_insns; ++i) {
        MipsDecodedInsn& insn = block_ctx_.insns[i];
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
            EmitMovRegImm32(code_location, kEcx, insn.guest_address + insn.length);
            EmitMovRegImm32(code_location, kEdx, self);
            EmitCall(code_location, reinterpret_cast<void*>(&NullifyLikelyHelper));
            EmitTestRegReg(code_location, kEax, kEax);
            uint8_t* j_run = EmitJzLabel(code_location);
            EmitRet(code_location);
            FixupLabel(j_run, code_location);
            if (j_skip_gate) FixupLabel(j_skip_gate, code_location);
        }

        if (i == 0 && block_ctx_.insn0_pcrel_guard) {
            EmitMovRegBaseDisp32(code_location, kEax, kStateReg, kBranchStateOff);
            EmitTestRegReg(code_location, kEax, kEax);
            uint8_t* j_not_ds = EmitJzLabel(code_location);
            EmitPush32(code_location, insn.guest_address);
            EmitPush32(code_location, self);
            EmitCall(code_location, reinterpret_cast<void*>(&PcrelDelaySlotHelper));
            FixupLabel(j_not_ds, code_location);
        }

        code_location = insn.place_fn(code_location, &insn, &block_ctx_);

        if (insn.ends_block) {
            /* The helper already set pc (EretHelper from EPC/ErrorEPC,
               HibernateHelper to the next insn); suppress the straight-line
               pc override and exit. */
            EmitRet(code_location);
            terminated = true;
            break;
        }
        if (i > 0 && block_ctx_.insns[i - 1].is_branch) {
            /* Within-block delay slot (block's last insn): branch_state is pending
               (set by insns[i-1]); resolve and exit (QEMU gen_branch). */
            EmitMovRegImm32(code_location, kEcx, insn.guest_address + insn.length);
            EmitMovRegImm32(code_location, kEdx, self);
            EmitCall(code_location, reinterpret_cast<void*>(&ResolveBranchHelper));
            EmitRet(code_location);
            terminated = true;
            break;
        }
        if (i == 0 && !insn.is_branch) {
            /* insn[0] may be a delay slot entered from a branch in the prior block
               (branch_state pending). Resolve-if-pending; a normal entry returns 0
               and the block continues (QEMU delay-slot-entry TB + gen_branch). */
            EmitMovRegImm32(code_location, kEcx, insn.guest_address + insn.length);
            EmitMovRegImm32(code_location, kEdx, self);
            EmitCall(code_location, reinterpret_cast<void*>(&ResolveBranchHelper));
            EmitTestRegReg(code_location, kEax, kEax);
            uint8_t* j_continue = EmitJzLabel(code_location);
            EmitRet(code_location);
            FixupLabel(j_continue, code_location);
        }
    }

    if (!terminated) {
        const MipsDecodedInsn& last = block_ctx_.insns[block_ctx_.num_insns - 1];
        const uint32_t next_pc = last.guest_address + last.length;
        EmitMovBaseDisp32Imm32(code_location, kStateReg, kPcOff, next_pc);
        EmitRet(code_location);
    }

    return static_cast<size_t>(code_location - start);
}

void* MipsBlockCompiler::FindBlockNativeStart(uint32_t guest_pc) {
    const JumpCacheEntry* e =
        cache_->Space(cpu_state_->isa_mode != 0u).JumpCacheProbe(guest_pc);
    if (!e) {
        return nullptr;
    }
    /* A per-entry TLBWI/TLBWR clears only the remapped page's 4 KB jump-cache
       window (JumpCacheClearPage), so a straddling block whose guest_start
       lies in another window keeps its entry across a remap of its tail page
       (QEMU cpu-exec.c tb_lookup validates the hit). */
    if (e->blk && e->blk->index_split != 0u && !TailStillMapped(e->blk)) {
        return nullptr;
    }
    return e->native;
}

bool MipsBlockCompiler::TailStillMapped(const JitBlock* blk) {
    if (blk->index_split == 0u) {
        return true;
    }
    uint32_t pa = 0;
    if (mmu_->Translate(cpu_state_, blk->guest_start + blk->index_split,
                        MipsAccess::kFetch, &pa) != MipsTlbResult::kMatch) {
        return false;
    }
    return cache_->BlockIndexKey(pa) == blk->index_start2;
}

void* MipsBlockCompiler::Compile(uint32_t guest_pc) {
    /* MIPS16 instructions sit at halfword boundaries ("the instruction is
       located at the halfword boundary", U15509EJ2V0UM Table 3-19 p82 JR). */
    const bool m16 = cpu_state_->isa_mode != 0u;
    IsaBlockSpace& space = cache_->Space(m16);
    if (guest_pc & (m16 ? 0x1u : 0x3u)) {
        /* Unaligned instruction fetch -> AdEL (MIPS PC must be word-aligned). CE's
           PSL implicit-call trap relies on it: coredll jumps to an unaligned magic
           VA and the kernel decodes the fault EPC into the API method. */
        exceptions_->DeliverFetchAddressError(guest_pc);
        return nullptr;
    }

    uint32_t pa0 = 0;
    const MipsTlbResult fr =
        mmu_->Translate(cpu_state_, guest_pc, MipsAccess::kFetch, &pa0);
    if (fr != MipsTlbResult::kMatch) {
        exceptions_->DeliverFetchTlbException(guest_pc, fr);
        return nullptr;
    }
    /* Block phys-identity + decode span are bounded to the SoC minimum page
       (1<<min_page_shift): a block that crossed a min-page boundary would have a
       phys_start that only pins its first page, so an independent remap of a later
       page would go undetected (invisible at 4 KB / VR5500; real at 1 KB / VR4102). */
    const uint32_t page_off_mask = (1u << cpu_state_->min_page_shift) - 1u;
    block_ctx_.block_phys_page_base = pa0 & ~page_off_mask;
    const uint32_t phys_start =
        block_ctx_.block_phys_page_base | (guest_pc & page_off_mask);

    const uint8_t asid = static_cast<uint8_t>(cpu_state_->cp0_entryhi & 0xFFu);
    const bool outer_global = mmu_->ExecPageGlobal(cpu_state_, guest_pc);
    JitBlockIndex& idx = outer_global ? space.global : space.per_asid[asid];

    JitBlock* ex = idx.FindExact(guest_pc);
    if (ex) {
        if (ex->native_start && ex->phys_start == phys_start &&
            TailStillMapped(ex)) {
            space.JumpCacheInsert(guest_pc, ex->native_start, ex);
            return ex->native_start;
        }
        space.RemoveBlock(ex);
    }

    block_ctx_.tail_split          = 0;
    block_ctx_.tail_page_pa        = 0;
    block_ctx_.fetch_fault_pending = 0;
    block_ctx_.insn0_pcrel_guard   = 0;
    if (m16) decoder_->Decode16(&block_ctx_, guest_pc);
    else     decoder_->Decode(&block_ctx_, guest_pc);
    if (block_ctx_.num_insns == 0) {
        if (block_ctx_.fetch_fault_pending) {
            exceptions_->DeliverFetchTlbException(block_ctx_.fetch_fault_va,
                                                  block_ctx_.fetch_fault_res);
            return nullptr;
        }
        LOG(Caution, "MipsBlockCompiler::Compile: decoded 0 insns at 0x%08X\n",
            guest_pc);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    /* One slab holds the outer entrypoint record + the emitted code. The
       per-insn upper bound (128 B) far exceeds any current place fn. */
    const size_t code_est  = static_cast<size_t>(block_ctx_.num_insns) * 128u + 64u;
    const size_t slab_size = JitBlockIndex::OuterEntrySize() + code_est;
    JitCodeArena& arena = cache_->Arena();
    uint8_t* slab = arena.Allocate(slab_size);
    if (!slab) {
        cache_->Flush();
        slab = arena.Allocate(slab_size);
        if (!slab) {
            LOG(Caution, "MipsBlockCompiler::Compile: arena exhausted (%zu bytes)\n",
                slab_size);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
    }
    uint8_t* code = slab + JitBlockIndex::OuterEntrySize();

    const MipsDecodedInsn& last = block_ctx_.insns[block_ctx_.num_insns - 1];
    JitBlock nb{};
    nb.guest_start  = guest_pc;
    nb.guest_end    = last.guest_address + last.length - 1u;
    nb.phys_start   = phys_start;
    nb.native_start = code;
    JitBlock* stored = idx.PlaceOuterAt(slab, nb);
    space.IndexInsert(stored, &idx, cache_->BlockIndexKey(phys_start),
                      block_ctx_.tail_split,
                      block_ctx_.tail_split != 0u
                          ? cache_->BlockIndexKey(block_ctx_.tail_page_pa)
                          : kBlockUnindexed);
    if (!outer_global) space.MarkPopulated(asid);

    const size_t code_size = GenerateCode(code);
    arena.FreeUnusedTail(code + code_size);

    space.JumpCacheInsert(guest_pc, stored->native_start, stored);
    return stored->native_start;
}
