#include "mips_jit.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstring>

#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
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
        case MipsOp::kORI:   return &PlaceMipsOri;
        case MipsOp::kJ:     return &PlaceMipsJ;
        case MipsOp::kJAL:   return &PlaceMipsJal;
        case MipsOp::kSPECIAL:
            if (d->raw == 0u)                      return &PlaceMipsNop;  /* SLL r0,r0,0 */
            if (d->funct == MipsSpecial::kADDU)    return &PlaceMipsAddu;
            if (d->funct == MipsSpecial::kOR)      return &PlaceMipsOr;
            if (d->funct == MipsSpecial::kJR)      return &PlaceMipsJr;
            if (d->funct == MipsSpecial::kJALR)    return &PlaceMipsJalr;
            return &PlaceMipsUndefined;
        case MipsOp::kCOP0:
            if (d->rs == MipsCop0Rs::kMTC0)        return &PlaceMipsMtc0;
            return &PlaceMipsUndefined;
        default:
            return &PlaceMipsUndefined;
    }
}

}  // namespace

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
            break;  /* TLB/address fault: JitCompile raises the CP0 exception */
        }
        uint8_t* host = memory_->TryTranslate(pa);
        if (!host) {
            break;  /* unmapped or I/O space: not executable */
        }

        uint32_t word;
        std::memcpy(&word, host, sizeof(word));
        decoder_.Decode(word, guest_pc, &insn);
        insn.place_fn = SelectPlaceFn(&insn);

        if (delay_pending) {            /* this insn was the branch's delay slot */
            ++i;
            break;                      /* block ends after the delay slot */
        }
        if (insn.is_branch) {
            delay_pending = true;       /* next insn is the delay slot, then end */
        }
    }

    /* Drop a trailing branch whose delay slot wasn't captured (page end / insn
       cap / unfetchable slot): JitGenerateCode would treat the block as
       straight-line and overwrite the branch's pc. It re-heads the next block. */
    if (i > 0 && block_ctx_.insns[i - 1].is_branch) {
        --i;
    }

    block_ctx_.num_insns = i;
}

void* MipsJit::FindBlockNativeStart(uint32_t guest_pc) {
    if (void* n = blocks_.JumpCacheLookup(guest_pc)) {
        return n;
    }
    JitBlock* b = blocks_.global.FindExact(guest_pc);
    if (!b) {
        const uint8_t asid = static_cast<uint8_t>(cpu_state_.cp0_entryhi & 0xFFu);
        b = blocks_.per_asid[asid].FindExact(guest_pc);
    }
    if (b && b->native_start) {
        blocks_.JumpCacheInsert(guest_pc, b->native_start);
        return b->native_start;
    }
    return nullptr;
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

    for (uint32_t i = 0; i < block_ctx_.num_insns; ++i) {
        MipsDecodedInsn& insn = block_ctx_.insns[i];
        EmitAddBaseDisp32Imm8(code_location, kStateReg, kCycleOff, 1);
        code_location = insn.place_fn(code_location, &insn, &block_ctx_);
    }

    /* JitDecode ends a block after a branch's delay slot, so a branch-terminated
       block has the branch at [num-2] (its place fn sets pc). A straight-line
       block (hit page end / insn cap) needs pc set to the next sequential PC. */
    const bool branch_terminated =
        block_ctx_.num_insns >= 2 &&
        block_ctx_.insns[block_ctx_.num_insns - 2].is_branch;
    if (!branch_terminated) {
        const uint32_t next_pc =
            block_ctx_.insns[block_ctx_.num_insns - 1].guest_address + 4u;
        EmitMovBaseDisp32Imm32(code_location, kStateReg, kPcOff, next_pc);
    }
    EmitRetn(code_location, 0);

    return static_cast<size_t>(code_location - start);
}
