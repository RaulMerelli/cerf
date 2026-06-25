#include "mips_jit.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstring>

#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../cpu/mips_processor_config.h"
#include "mips_cp0_emitter.h"
#include "../../boards/page_table_builder.h"
#include "../../boot/rom_parser_service.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../tracing/trace_manager.h"
#include "../x86_emit.h"
#include "mips_place_fns.h"

namespace {

int MipsDispatchFaultFilter(EXCEPTION_POINTERS* ep, uint32_t guest_pc,
                            bool* host_fault) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == MipsJit::kGuestExceptionCode) {
        /* A guest CP0 exception a memory/arith helper raised: pc + CP0 already
           point at the vector, so the handler returns cleanly and the next run
           loop iteration dispatches the handler. */
        *host_fault = false;
        return EXCEPTION_EXECUTE_HANDLER;
    }
    *host_fault = true;
    LOG(Caution,
        "MipsJit: host exception 0x%08lX at host addr %p while running guest PC 0x%08X\n",
        code, ep->ExceptionRecord->ExceptionAddress, guest_pc);
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

REGISTER_SERVICE_AS(MipsJit, GuestEngine);

MipsJit::~MipsJit() {
    if (idle_event_) {
        CloseHandle(idle_event_);
        idle_event_ = nullptr;
    }
}

void MipsJit::OnReady() {
    LOG(Jit, "MipsJit::OnReady: resolving dependencies\n");
    memory_     = &emu_.Get<EmulatedMemory>();
    peripheral_ = &emu_.Get<PeripheralDispatcher>();

    arena_.Initialize();

    /* Size the per-physical-page block index over the board's DRAM extent. */
    const auto dram = emu_.Get<PageTableBuilder>().CachedDramRegions();
    uint32_t pg_base = 0, pg_count = 0;
    if (!dram.empty()) {
        pg_base  = dram.front().pa_base >> 12;
        pg_count = dram.front().size   >> 12;
    }
    blocks_.Initialize(pg_base, pg_count);
    mmu_.Bind(&blocks_);

    /* Per-SoC CPU silicon facts come from MipsProcessorConfig, not the engine -
       the MIPS analog of seeding ArmProcessorConfig MIDR / HasX(). */
    auto& cpu_cfg = emu_.Get<MipsProcessorConfig>();
    cpu_config_           = &cpu_cfg;
    cp0_emitter_          = &emu_.Get<MipsCp0Emitter>();
    cpu_state_.cp0_prid   = cpu_cfg.Prid();
    cpu_state_.nb_tlb     = cpu_cfg.TlbSize();
    cpu_state_.tlb_in_use = cpu_state_.nb_tlb;

    /* The decoder implements MIPS ISA IV only. */
    if (cpu_cfg.IsaLevel() != MipsIsaLevel::kMips4) {
        LOG(Caution, "MipsJit: unsupported ISA level %u (engine implements MIPS IV)\n",
            static_cast<uint32_t>(cpu_cfg.IsaLevel()));
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    decoder_.Configure(cpu_cfg.HasFpu(), cpu_cfg.HasLlsc());

    idle_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!idle_event_) {
        LOG(Caution, "MipsJit: CreateEventW(idle_event) failed gle=%lu\n",
            GetLastError());
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    block_ctx_.jit = this;

    /* Cold reset PC is the kernel entry kseg0 VA itself: the VR5500 fetches
       kseg0 through the unmapped window (MMU folds it to PA), so feeding a
       physical address here would re-enter JitCompile translating a PA as a
       kuseg VA and TLB-miss. */
    cpu_state_.pc = emu_.Get<RomParserService>().EntryVa();

    LOG(Jit, "MipsJit::OnReady: bringup done; entry VA=0x%08X dram_pa_base=0x%08X "
             "pg_count=%u\n",
        cpu_state_.pc, pg_base << 12, pg_count);
}

void* MipsJit::JitCompile(uint32_t guest_pc) {
    guest_pc &= ~0x3u;

    uint32_t pa0 = 0;
    const MipsTlbResult fr =
        mmu_.Translate(&cpu_state_, guest_pc, MipsAccess::kFetch, &pa0);
    if (fr != MipsTlbResult::kMatch) {
        /* Instruction-fetch TLB miss/invalid: deliver TLBL so the guest's refill
           handler maps the page, then bail - Run re-dispatches at the vector. */
        DeliverFetchTlbException(guest_pc, fr);
        return nullptr;
    }
    block_ctx_.block_phys_page_base = pa0 & 0xFFFFF000u;
    const uint32_t phys_start = block_ctx_.block_phys_page_base | (guest_pc & 0xFFFu);

    /* Global (kseg / G=1) blocks live in the shared `global` index; per-process
       (G=0) blocks in per_asid[ASID]. */
    const uint8_t asid = static_cast<uint8_t>(cpu_state_.cp0_entryhi & 0xFFu);
    const bool outer_global = mmu_.ExecPageGlobal(&cpu_state_, guest_pc);
    JitBlockIndex& idx = outer_global ? blocks_.global : blocks_.per_asid[asid];

    JitBlock* ex = blocks_.per_asid[asid].FindExact(guest_pc);
    if (!ex) ex = blocks_.global.FindExact(guest_pc);
    if (ex) {
        if (ex->native_start && ex->phys_start == phys_start) {
            blocks_.JumpCacheInsert(guest_pc, ex->native_start);
            return ex->native_start;     /* phys-checked reuse */
        }
        blocks_.RemoveExact(guest_pc);   /* stale phys at same VA - evict */
    }

    JitDecode(guest_pc);
    if (block_ctx_.num_insns == 0) {
        LOG(Caution, "MipsJit::JitCompile: decoded 0 insns at 0x%08X\n", guest_pc);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    /* One slab holds the outer entrypoint record + the emitted code. The
       per-insn upper bound (128 B) far exceeds any current place fn. */
    const size_t code_est  = static_cast<size_t>(block_ctx_.num_insns) * 128u + 64u;
    const size_t slab_size = JitBlockIndex::OuterEntrySize() + code_est;
    uint8_t* slab = arena_.Allocate(slab_size);
    if (!slab) {
        arena_.Flush();
        blocks_.FlushAll();
        slab = arena_.Allocate(slab_size);
        if (!slab) {
            LOG(Caution, "MipsJit::JitCompile: arena exhausted (%zu bytes)\n", slab_size);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
    }
    uint8_t* code = slab + JitBlockIndex::OuterEntrySize();

    JitBlock nb{};
    nb.guest_start  = guest_pc;
    nb.guest_end    = block_ctx_.insns[block_ctx_.num_insns - 1].guest_address + 3u;
    nb.phys_start   = phys_start;
    nb.native_start = code;
    JitBlock* stored = idx.PlaceOuterAt(slab, nb);
    blocks_.IndexInsert(stored, &idx);
    if (!outer_global) blocks_.MarkPopulated(asid);

    const size_t code_size = JitGenerateCode(code, 1);
    arena_.FreeUnusedTail(code + code_size);
    stored->native_end = code + code_size;

    blocks_.JumpCacheInsert(guest_pc, stored->native_start);
    return stored->native_start;
}

void MipsJit::JitDecode(uint32_t guest_pc) {
    guest_pc &= ~0x3u;
    const uint32_t page_end = (guest_pc & 0xFFFFF000u) + 0x1000u;
    std::memset(block_ctx_.insns, 0, sizeof(block_ctx_.insns));

    uint32_t i = 0;
    bool delay_pending = false;  /* prior insn was a branch; this is its delay slot */
    for (; i < kMaxMipsInsnPerBlock && guest_pc < page_end; ++i, guest_pc += 4) {
        MipsDecodedInsn& insn = block_ctx_.insns[i];

        uint32_t pa = 0;
        if (mmu_.Translate(&cpu_state_, guest_pc, MipsAccess::kFetch, &pa) !=
            MipsTlbResult::kMatch) {
            break;  /* TLB/address fault */
        }
        uint8_t* host = memory_->TryTranslate(pa);
        if (!host) {
            break;  /* unmapped or I/O space: not executable */
        }

        uint32_t word;
        std::memcpy(&word, host, sizeof(word));
        if (decoder_.Decode(word, guest_pc, &insn)) {
            insn.place_fn = SelectPlaceFn(&insn);
        } else {
            /* Encoding not valid for this CPU's capabilities (COP1 when !HasFpu,
               LL/SC when !HasLlsc, or a reserved opcode) -> Reserved Instruction.
               CP0 exception delivery is not yet built, so it surfaces loudly. */
            insn.place_fn = &PlaceMipsUndefined;
        }

        if (delay_pending) {            /* this insn was the branch's delay slot */
            ++i;
            break;                      /* block ends after the delay slot */
        }
        if (insn.is_eret) {             /* ERET ends the block; it has no delay slot */
            ++i;
            break;
        }
        if (insn.is_branch) {
            delay_pending = true;       /* next insn is the delay slot, then end */
        }
    }

    /* A trailing branch (page end / cap before its delay slot) is KEPT, not
       dropped: its place fn set branch_state, the block exits with pc=branch+4,
       and the carried state resolves in the next block (QEMU DISAS_TOO_MANY +
       save_cpu_state). Dropping it was the cross-page "decoded 0 insns" defect. */
    block_ctx_.num_insns = i;
}

void* MipsJit::FindBlockNativeStart(uint32_t guest_pc) {
    /* VA jump-cache lookup; null on miss -> Run() routes to JitCompile. */
    return blocks_.JumpCacheLookup(guest_pc);
}

void MipsJit::ContextSwitchFlush() {
    blocks_.JumpCacheFlush();
}

void __fastcall MipsJit::Mtc0EntryHiHelper(uint32_t value, MipsJit* jit) {
    /* helper_mtc0_entryhi (cp0_helper.c:1142): write VPN2+ASID, preserve the
       reserved field, flush on an ASID change. mask = (TARGET_PAGE_MASK<<1)|ASID. */
    constexpr uint32_t kMask = 0xFFFFE0FFu;
    MipsCpuState& s = jit->cpu_state_;
    const uint32_t old = s.cp0_entryhi;
    const uint32_t val = (value & kMask) | (old & ~kMask);
    s.cp0_entryhi = val;
    if ((old & 0xFFu) != (val & 0xFFu)) {
        jit->ContextSwitchFlush();
    }
}

void MipsJit::SignalIdleWake() {
    if (idle_event_) SetEvent(idle_event_);
}

void MipsJit::SetExternalInterruptLevel(uint32_t ip_mask) {
    external_ip_.store(ip_mask, std::memory_order_release);
    SignalIdleWake();
}

void MipsJit::Run() {
    if (cpu_state_.reset_pending) { DeliverReset(); return; }
    /* branch_state==kNone gate: never take an interrupt between a branch and its
       not-yet-run delay slot, or the delay slot is skipped on ERET resume. */
    TimerPoll();
    /* Fold the INTC-driven device IRQ level (IP5:2) into cp0_cause on this thread
       (cp0_cause is JIT-owned; only external_ip_ crosses threads). */
    constexpr uint32_t kDeviceIpMask = 0x00003C00u;   /* Cause.IP2..IP5 (bits 10..13) */
    cpu_state_.cp0_cause =
        (cpu_state_.cp0_cause & ~kDeviceIpMask) |
        (external_ip_.load(std::memory_order_acquire) & kDeviceIpMask);
    if (cpu_state_.branch_state == MipsBranch::kNone && InterruptReady()) {
        DeliverInterrupt();
    }
    const uint32_t pc = cpu_state_.pc;
    void* native = FindBlockNativeStart(pc);
    if (!native) {
        native = JitCompile(pc);
        if (!native) return;  /* fetch TLB exception delivered; pc=vector, next Run() dispatches it */
    }
    bool host_fault = false;
    __try {
        Dispatch(native, &cpu_state_);
    } __except (MipsDispatchFaultFilter(GetExceptionInformation(), pc, &host_fault)) {
        if (host_fault) {
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
    }
}

void __cdecl MipsJit::UnimplementedHelper(MipsJit* /* jit */, uint32_t pc, uint32_t raw) {
    LOG(Caution, "MipsJit: unimplemented instruction 0x%08X at guest PC 0x%08X\n",
        raw, pc);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void __cdecl MipsJit::ArithOverflowHelper(MipsJit* jit, uint32_t /* pc */) {
    /* The faulting PC is already live in cpu_state_.pc (per-insn materialization);
       RaiseOverflowException reads it for EPC and never returns (SEH unwind). */
    jit->RaiseOverflowException();
}

void __cdecl MipsJit::TraceDispatchPcHelper(MipsJit* jit, uint32_t pc) {
    jit->emu_.Get<TraceManager>().DispatchPcMips(pc, &jit->cpu_state_);
}

std::optional<uint8_t*> MipsJit::PeekGuestVa(uint32_t va) {
    uint32_t pa = 0;
    if (mmu_.Translate(&cpu_state_, va, MipsAccess::kRead, &pa) !=
        MipsTlbResult::kMatch) {
        return std::nullopt;
    }
    uint8_t* host = memory_->TryTranslate(pa);
    if (!host) return std::nullopt;
    return host;
}

uint8_t* MipsJit::ResolveGuestVaToHost(uint32_t va) {
    uint32_t pa = 0;
    if (mmu_.Translate(&cpu_state_, va, MipsAccess::kRead, &pa) !=
        MipsTlbResult::kMatch) {
        return nullptr;
    }
    uint8_t* w = memory_->TryTranslateWrite(pa);
    return w ? w : memory_->TryTranslate(pa);
}

void __fastcall MipsJit::TlbwiHelper(MipsJit* jit) {
    jit->mmu_.WriteIndexed(&jit->cpu_state_);
}

void __fastcall MipsJit::TlbwrHelper(MipsJit* jit) {
    jit->mmu_.WriteRandom(&jit->cpu_state_);
}

void __fastcall MipsJit::TlbpHelper(MipsJit* jit) {
    jit->mmu_.Probe(&jit->cpu_state_);
}

void __fastcall MipsJit::TlbrHelper(MipsJit* jit) {
    jit->mmu_.Read(&jit->cpu_state_);
}

int __fastcall MipsJit::ResolveBranchHelper(uint32_t fallthrough, MipsJit* jit) {
    MipsCpuState& s = jit->cpu_state_;
    if (s.branch_state == MipsBranch::kNone) {
        return 0;
    }
    s.pc = (s.branch_state == MipsBranch::kCond)
               ? (s.bcond ? s.btarget : fallthrough)  /* gen_branch BC */
               : s.btarget;                            /* gen_branch B / BR / BL-taken */
    s.branch_state = MipsBranch::kNone;
    return 1;
}

int __fastcall MipsJit::NullifyLikelyHelper(uint32_t fallthrough, MipsJit* jit) {
    MipsCpuState& s = jit->cpu_state_;
    if (s.branch_state == MipsBranch::kCondLikely && s.bcond == 0) {
        s.pc = fallthrough;            /* skip the delay slot (QEMU pc_next+4) */
        s.branch_state = MipsBranch::kNone;
        return 1;
    }
    return 0;                          /* taken, or not a likely delay slot */
}

/* __cdecl(native_pc, state). After the 4 register PUSHes the args sit at
   [esp+20] and [esp+24]. ESI is pinned to MipsCpuState* for the duration of
   the translated block; ebp/ebx/edi are preserved for the C++ caller. */
__declspec(naked) void __cdecl MipsJit::Dispatch(void* /* native_pc */,
                                                 MipsCpuState* /* state */) {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov  ecx, [esp + 20]
        mov  esi, [esp + 24]
        call ecx
        pop  edi
        pop  esi
        pop  ebx
        pop  ebp
        ret
    }
}

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

