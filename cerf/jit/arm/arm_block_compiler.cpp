#include "arm_block_compiler.h"

#include <cstring>

#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../core/log.h"
#include "../../core/rate_probe.h"
#include "../../cpu/arm_processor_config.h"
#include "../../tracing/trace_manager.h"
#include "../x86_emit.h"
#include "../x86_emit_alu.h"
#include "arm_cpu.h"
#include "arm_decoder.h"
#include "thumb32_decoder.h"
#include "thumb_decoder.h"
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

/* ARM ARM DDI 0406C.c Table A8-1, p. A8-288: EQ Z==1, NE Z==0, CS C==1,
   CC C==0, MI N==1, PL N==0, VS V==1, VC V==0, HI C==1 and Z==0, LS C==0 or
   Z==1, GE N==V, LT N!=V, GT Z==0 and N==V, LE Z==1 or N!=V. */
uint8_t* EmitConditionGuard(uint8_t*& cursor, uint32_t cond) {
    using namespace x86;
    switch (cond) {
    case 0u:
    case 1u:
        EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmZfDisp(), 0u);
        return cond == 0u ? EmitJzLabel32(cursor) : EmitJnzLabel32(cursor);
    case 2u:
    case 3u:
        EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmCfDisp(), 0u);
        return cond == 2u ? EmitJzLabel32(cursor) : EmitJnzLabel32(cursor);
    case 4u:
    case 5u:
        EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmNfDisp(), 0u);
        return cond == 4u ? EmitJzLabel32(cursor) : EmitJnzLabel32(cursor);
    case 6u:
    case 7u:
        EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmVfDisp(), 0u);
        return cond == 6u ? EmitJzLabel32(cursor) : EmitJnzLabel32(cursor);
    case 8u:
    case 9u:
        EmitMovByteRegBaseDisp32(cursor, kAl, kStateReg, ArmCfDisp());
        EmitSubByteRegBaseDisp32(cursor, kAl, kStateReg, ArmZfDisp());
        EmitCmpReg8Imm8(cursor, kAl, 1u);
        return cond == 8u ? EmitJnzLabel32(cursor) : EmitJzLabel32(cursor);
    case 10u:
    case 11u:
        EmitMovByteRegBaseDisp32(cursor, kAl, kStateReg, ArmNfDisp());
        EmitCmpReg8BaseDisp32(cursor, kAl, kStateReg, ArmVfDisp());
        return cond == 10u ? EmitJnzLabel32(cursor) : EmitJzLabel32(cursor);
    case 12u:
    case 13u:
        EmitMovByteRegBaseDisp32(cursor, kAl, kStateReg, ArmNfDisp());
        EmitXorByteRegBaseDisp32(cursor, kAl, kStateReg, ArmVfDisp());
        EmitOrByteRegBaseDisp32(cursor, kAl, kStateReg, ArmZfDisp());
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
    thumb_decoder_ = &emu_.Get<ThumbDecoder>();
    thumb32_decoder_ = &emu_.Get<Thumb32Decoder>();
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
                                                  ArmPackCpsr(*compiler->cpu_state_));
}

void ArmBlockCompiler::Decode(uint32_t guest_pc, uint32_t folded_pc) {
    const uint32_t page_end =
        (folded_pc & ~(kArmBlockPageBytes - 1u)) + kArmBlockPageBytes;
    fetch_fault_ = false;
    wide_split_  = 0u;
    uint8_t thumb_itstate = 0u;

    uint32_t i = 0;
    for (; i < kMaxArmInsnPerBlock && folded_pc < page_end; ++i) {
        DecodedInsn& insn = block_ctx_.insns[i];
        std::memset(&insn, 0, sizeof(insn));

        uint8_t* host = walker_->TranslateExecute(cpu_state_, guest_pc);
        if (host == nullptr) break;

        insn.guest_address        = guest_pc;
        insn.actual_guest_address = folded_pc;

        bool decoded;
        if (thumb_) {
            uint16_t half = 0u;
            std::memcpy(&half, host, sizeof(half));
            bool is_it = false;
            /* ARM DDI 0406C.c A8.8.55: IT carries firstcond and a nonzero
               mask; the ITSTATE then predicates each following instruction.
               Keep four decoded slots available so a JIT block cannot end
               between IT and the longest possible controlled sequence. */
            if ((half & 0xFF00u) == 0xBF00u && (half & 0x000Fu) != 0u) {
                if (i + 4u >= kMaxArmInsnPerBlock && i != 0u) break;
                const uint32_t firstcond = (half >> 4) & 0xFu;
                if (firstcond == 0xFu) {
                    decoded = false;
                } else {
                    thumb_itstate = static_cast<uint8_t>(
                        (firstcond << 4) | (half & 0xFu));
                    insn.cond     = 14u;
                    insn.length   = 2u;
                    insn.place_fn = &PlaceNop;
                    decoded       = true;
                    is_it         = true;
                }
            } else if (thumb32_decoder_->IsWide(half)) {
                const bool crosses = folded_pc + 4u > page_end;
                if (crosses && i != 0u) break;
                uint8_t* hi =
                    walker_->TranslateExecute(cpu_state_, guest_pc + 2u);
                if (hi == nullptr) {
                    if (i == 0u) {
                        fetch_fault_    = true;
                        fetch_fault_pc_ = guest_pc;
                    }
                    break;
                }
                if (crosses) {
                    wide_split_ = 2u;
                    wide_pa2_   = walker_->LastExecPa();
                }
                uint16_t lo = 0u;
                std::memcpy(&lo, hi, sizeof(lo));
                insn.length = 4u;
                decoded     = thumb32_decoder_->DecodeThumb32(
                    &insn, (static_cast<uint32_t>(half) << 16) | lo);
            } else {
                insn.length = 2u;
                decoded     = thumb_decoder_->DecodeThumb(&insn, half);
            }

            if (decoded && thumb_itstate != 0u && !is_it) {
                insn.cond = thumb_itstate >> 4;
                /* ARM DDI 0406C.c A8.8.102/.4/.8: the Thumb T1 MOV/ADD/SUB
                   forms use setflags = !InITBlock(). The test operations
                   (TST/CMP/CMN, data-processing opcodes 8, 10 and 11) keep
                   their mandatory flag update. */
                const bool data_processing =
                    insn.place_fn == &PlaceDataProcessing ||
                    insn.place_fn == &PlaceDataProcessingReg ||
                    insn.place_fn == &PlaceDataProcessingShiftedReg ||
                    insn.place_fn == &PlaceMultiply;
                const bool mandatory_flags =
                    data_processing &&
                    (insn.op1 == 8u || insn.op1 == 10u || insn.op1 == 11u);
                if (data_processing && insn.length == 2u && insn.s != 0u &&
                    !mandatory_flags) {
                    insn.s = 0u;
                }
                if ((thumb_itstate & 7u) == 0u) {
                    thumb_itstate = 0u;
                } else {
                    thumb_itstate = static_cast<uint8_t>(
                        (thumb_itstate & 0xE0u) |
                        ((thumb_itstate << 1) & 0x1Fu));
                }
            }
        } else {
            ArmOpcode op{};
            std::memcpy(&op.word, host, sizeof(op.word));
            insn.length = 4u;
            decoded     = decoder_->DecodeArm(&insn, op);
        }

        if (!decoded) {
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

        guest_pc  += insn.length;
        folded_pc += insn.length;
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
                               last.guest_address + last.length);
        cursor = EmitChainToBlock(cursor, &block_ctx_,
                                  last.guest_address + last.length, 1u);
        EmitRet(cursor);
    }

    return static_cast<size_t>(cursor - start);
}

void* ArmBlockCompiler::Compile(uint32_t guest_pc) {
    /* BranchWritePC / BXWritePC (DDI 0406C.c A2.3.2, p. A2-47): an ARM-state
       branch target always has address<1:0> == '00'. ARM DDI 0100I Figure A6-1
       (A6.2, p. A6-4) encodes Thumb in bits[15:0], so a Thumb target is
       halfword-aligned. */
    const bool     thumb      = cpu_state_->cpsr.bits.thumb_mode != 0u;
    const uint32_t align_mask = thumb ? 1u : 3u;
    thumb_                    = thumb;
    if ((guest_pc & align_mask) != 0u) {
        LOG(Caution, "ArmBlockCompiler::Compile: misaligned PC 0x%08X\n",
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
    IsaBlockSpace& space        = cache_->Space(thumb);
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
        if (fetch_fault_) {
            if (mmu_->io_pending()) {
                emu_.Get<Fatal>().Die(
                    "ArmBlockCompiler::Compile: instruction fetch at 0x%08X "
                    "resolves to peripheral PA 0x%08X\n",
                    fetch_fault_pc_ + 2u, mmu_->io_pending_address());
            }
            cpu_->RaiseAbortPrefetchException(fetch_fault_pc_);
            return nullptr;
        }
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
            const DecodedInsn& tail =
                block_ctx_.insns[block_ctx_.num_insns - 1];
            nb.guest_end    = tail.actual_guest_address + tail.length - 1u;
            nb.phys_start   = phys_start;
            nb.native_start = code;
            JitBlock* stored = idx.PlaceOuterAt(slab, nb);
            space.IndexInsert(stored, &idx, phys_start, wide_split_,
                              wide_split_ != 0u ? wide_pa2_ : kBlockUnindexed);
            if (!outer_global) space.MarkPopulated(asid);

            block_ctx_.index       = &idx;
            block_ctx_.self        = stored;
            block_ctx_.guest_start = folded_pc;
            block_ctx_.phys_start  = phys_start;
            block_ctx_.fcse_pid    = mmu_state->process_id;
            block_ctx_.jump_cache  = space.jump_cache;
            block_ctx_.thumb       = thumb;

            JitBlock* const src = idx.FindExact(predecessor_va_);
            if (src != nullptr && src != stored &&
                phys_start ==
                    src->phys_start + (folded_pc - src->guest_start)) {
                for (uint32_t s = 0; s < 2u; ++s) {
                    if (src->chain_target[s] != nullptr ||
                        src->chain_site[s] == nullptr ||
                        src->chain_pending_va[s] != folded_pc) {
                        continue;
                    }
                    const uint32_t disp = static_cast<uint32_t>(
                        code - (src->chain_site[s] + 4));
                    std::memcpy(src->chain_site[s], &disp, 4);
                    idx.LinkChain(src, s, stored, src->chain_site[s],
                                  src->chain_fallback[s]);
                }
            }

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
