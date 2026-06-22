#include "mips_jit.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstring>

#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../cpu/mips_processor_config.h"
#include "../../boards/page_table_builder.h"
#include "../../boot/rom_parser_service.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../x86_emit.h"
#include "mips_opcode.h"
#include "mips_place_fns.h"

namespace {

int MipsDispatchFaultFilter(EXCEPTION_POINTERS* ep, uint32_t guest_pc) {
    LOG(Caution,
        "MipsJit: host exception 0x%08lX at host addr %p while running guest PC 0x%08X\n",
        ep->ExceptionRecord->ExceptionCode,
        ep->ExceptionRecord->ExceptionAddress,
        guest_pc);
    return EXCEPTION_EXECUTE_HANDLER;
}

/* Decode -> emit dispatch. Implemented opcodes map to their place fn; every
   other (recognized-but-unimplemented or reserved) maps to the loud-fatal stub
   so the bring-up loop surfaces it on first execution. */
MipsPlaceFn SelectPlaceFn(const MipsDecodedInsn* d) {
    switch (d->op) {
        case MipsOp::kLUI:   return &PlaceMipsLui;
        case MipsOp::kADDIU: return &PlaceMipsAddiu;
        case MipsOp::kADDI:  return &PlaceMipsAddi;
        case MipsOp::kSLTIU: return &PlaceMipsSltiu;
        case MipsOp::kSLTI:  return &PlaceMipsSlti;
        case MipsOp::kDADDIU: return &PlaceMipsDaddiu;
        case MipsOp::kORI:   return &PlaceMipsOri;
        case MipsOp::kANDI:  return &PlaceMipsAndi;
        case MipsOp::kJ:     return &PlaceMipsJ;
        case MipsOp::kJAL:   return &PlaceMipsJal;
        case MipsOp::kLW:    return &PlaceMipsLw;
        case MipsOp::kLD:    return &PlaceMipsLd;
        case MipsOp::kSW:    return &PlaceMipsSw;
        case MipsOp::kSH:    return &PlaceMipsSh;
        case MipsOp::kSB:    return &PlaceMipsSb;
        case MipsOp::kSD:    return &PlaceMipsSd;
        case MipsOp::kSDR:   return &PlaceMipsSdr;
        case MipsOp::kSDL:   return &PlaceMipsSdl;
        case MipsOp::kSWR:   return &PlaceMipsSwr;
        case MipsOp::kLWL:   return &PlaceMipsLwl;
        case MipsOp::kSWL:   return &PlaceMipsSwl;
        case MipsOp::kLB:    return &PlaceMipsLb;
        case MipsOp::kLHU:   return &PlaceMipsLhu;
        case MipsOp::kLBU:   return &PlaceMipsLbu;
        case MipsOp::kLWU:   return &PlaceMipsLwu;
        case MipsOp::kBGTZ:  return &PlaceMipsBgtz;
        case MipsOp::kBLEZ:  return &PlaceMipsBlez;
        case MipsOp::kPREF:  return &PlaceMipsNop;   /* prefetch hint: NOP (QEMU OPC_PREF translate.c:14676) */
        case MipsOp::kREGIMM:
            if (d->rt == MipsRegimm::kBLTZ)        return &PlaceMipsBltz;
            if (d->rt == MipsRegimm::kBGEZ)        return &PlaceMipsBgez;
            return &PlaceMipsUndefined;
        case MipsOp::kBEQ:   return &PlaceMipsBeq;
        case MipsOp::kBNE:   return &PlaceMipsBne;
        case MipsOp::kSPECIAL:
            if (d->raw == 0u)                      return &PlaceMipsNop;  /* SLL r0,r0,0 */
            if (d->funct == MipsSpecial::kSLL)     return &PlaceMipsSll;
            if (d->funct == MipsSpecial::kSRL)     return &PlaceMipsSrl;
            if (d->funct == MipsSpecial::kSRA)     return &PlaceMipsSra;
            if (d->funct == MipsSpecial::kSRLV)    return &PlaceMipsSrlv;
            if (d->funct == MipsSpecial::kSLLV)    return &PlaceMipsSllv;
            if (d->funct == MipsSpecial::kADD)     return &PlaceMipsAdd;
            if (d->funct == MipsSpecial::kADDU)    return &PlaceMipsAddu;
            if (d->funct == MipsSpecial::kSUBU)    return &PlaceMipsSubu;
            if (d->funct == MipsSpecial::kOR)      return &PlaceMipsOr;
            if (d->funct == MipsSpecial::kAND)     return &PlaceMipsAnd;
            if (d->funct == MipsSpecial::kXOR)     return &PlaceMipsXor;
            if (d->funct == MipsSpecial::kNOR)     return &PlaceMipsNor;
            if (d->funct == MipsSpecial::kDIVU)    return &PlaceMipsDivu;
            if (d->funct == MipsSpecial::kMFHI)    return &PlaceMipsMfhi;
            if (d->funct == MipsSpecial::kMFLO)    return &PlaceMipsMflo;
            if (d->funct == MipsSpecial::kSLTU)    return &PlaceMipsSltu;
            if (d->funct == MipsSpecial::kJR)      return &PlaceMipsJr;
            if (d->funct == MipsSpecial::kJALR)    return &PlaceMipsJalr;
            if (d->funct == MipsSpecial::kDADDU)   return &PlaceMipsDaddu;
            if (d->funct == MipsSpecial::kDSUBU)   return &PlaceMipsDsubu;
            if (d->funct == MipsSpecial::kMOVZ)    return &PlaceMipsMovz;
            if (d->funct == MipsSpecial::kDSLL32)  return &PlaceMipsDsll32;
            if (d->funct == MipsSpecial::kDSRL32)  return &PlaceMipsDsrl32;
            return &PlaceMipsUndefined;
        case MipsOp::kCOP0:
            if (d->rs == MipsCop0Rs::kMTC0)        return &PlaceMipsMtc0;
            if (d->rs == MipsCop0Rs::kMFC0)        return &PlaceMipsMfc0;
            if (d->rs >= MipsCop0Rs::kCO) {        /* CO bit set: dispatch on funct */
                if (d->funct == MipsCop0Funct::kTLBWI) return &PlaceMipsTlbwi;
            }
            return &PlaceMipsUndefined;
        default:
            return &PlaceMipsUndefined;
    }
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
    if (mmu_.Translate(&cpu_state_, guest_pc, MipsAccess::kFetch, &pa0) !=
        MipsTlbResult::kMatch) {
        LOG(Caution, "MipsJit::JitCompile: fetch fault at guest PC 0x%08X\n", guest_pc);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    block_ctx_.block_phys_page_base = pa0 & 0xFFFFF000u;
    const uint32_t phys_start = block_ctx_.block_phys_page_base | (guest_pc & 0xFFFu);

    if (JitBlock* ex = blocks_.global.FindExact(guest_pc)) {
        if (ex->native_start && ex->phys_start == phys_start) {
            blocks_.JumpCacheInsert(guest_pc, ex->native_start);
            return ex->native_start;
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
    JitBlock* stored = blocks_.global.PlaceOuterAt(slab, nb);
    blocks_.IndexInsert(stored, &blocks_.global);

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
    /* VA jump cache only: a miss returns null so Run() routes to JitCompile,
       which re-resolves phys from the fetch and reuses a cached block only on a
       phys_start match - so a TLB remap of a mapped-segment VA never runs a
       stale block (QEMU tb_jmp_cache fast path, tb-jmp-cache.h). */
    return blocks_.JumpCacheLookup(guest_pc);
}

void MipsJit::Run() {
    const uint32_t pc = cpu_state_.pc;
    void* native = FindBlockNativeStart(pc);
    if (!native) {
        native = JitCompile(pc);
    }
    __try {
        Dispatch(native, &cpu_state_);
    } __except (MipsDispatchFaultFilter(GetExceptionInformation(), pc)) {
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
}

void __cdecl MipsJit::UnimplementedHelper(MipsJit* /* jit */, uint32_t pc, uint32_t raw) {
    LOG(Caution, "MipsJit: unimplemented instruction 0x%08X at guest PC 0x%08X\n",
        raw, pc);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void __cdecl MipsJit::ArithOverflowHelper(MipsJit* /* jit */, uint32_t pc) {
    LOG(Caution, "MipsJit: integer overflow at guest PC 0x%08X "
            "(Integer Overflow exception delivery not yet implemented)\n", pc);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void __fastcall MipsJit::TlbwiHelper(MipsJit* jit) {
    jit->mmu_.WriteIndexed(&jit->cpu_state_);
}

int __fastcall MipsJit::ResolveBranchHelper(uint32_t fallthrough, MipsJit* jit) {
    MipsCpuState& s = jit->cpu_state_;
    if (s.branch_state == MipsBranch::kNone) {
        return 0;
    }
    s.pc = (s.branch_state == MipsBranch::kCond)
               ? (s.bcond ? s.btarget : fallthrough)  /* gen_branch BC */
               : s.btarget;                            /* gen_branch B / BR */
    s.branch_state = MipsBranch::kNone;
    return 1;
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
    const uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));

    bool terminated = false;  /* a within-block delay-slot resolve emitted the ret */

    for (uint32_t i = 0; i < block_ctx_.num_insns; ++i) {
        MipsDecodedInsn& insn = block_ctx_.insns[i];
        EmitAddBaseDisp32Imm8(code_location, kStateReg, kCycleOff, 1);
        code_location = insn.place_fn(code_location, &insn, &block_ctx_);

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

