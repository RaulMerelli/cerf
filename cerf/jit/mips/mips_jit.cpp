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
#include "mips_exception_model.h"
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

    const auto dram = emu_.Get<PageTableBuilder>().CachedDramRegions();
    if (dram.empty() || static_cast<int>(dram.size()) > kMaxDramRegions) {
        LOG(Caution, "MipsJit: board declares %zu cached-DRAM regions; the SMC "
                "block index supports 1..%d\n", dram.size(), kMaxDramRegions);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    uint32_t index_base = 0;
    for (const auto& r : dram) {
        uint8_t* host = memory_->TryTranslate(r.pa_base);
        if (!host || (r.size & 0xFFFu) != 0u) {
            LOG(Caution, "MipsJit: DRAM pa=0x%08X size=0x%X is not a page-multiple "
                    "backed region\n", r.pa_base, r.size);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        DramHostRegion& e = dram_regions_[dram_region_count_++];
        e.host_lo    = host;
        e.host_hi    = host + r.size;
        e.index_base = index_base;
        index_base  += r.size;
    }
    dram_index_size_ = index_base;
    blocks_.Initialize(0, dram_index_size_ >> 12);
    mmu_ = &emu_.Get<MipsMmu>();
    mmu_->Bind(&blocks_);

    /* Per-SoC CPU silicon facts come from MipsProcessorConfig, not the engine -
       the MIPS analog of seeding ArmProcessorConfig MIDR / HasX(). */
    auto& cpu_cfg = emu_.Get<MipsProcessorConfig>();
    cpu_config_           = &cpu_cfg;
    cp0_emitter_          = &emu_.Get<MipsCp0Emitter>();
    exception_            = &emu_.Get<MipsExceptionModel>();
    cpu_state_.cp0_prid   = cpu_cfg.Prid();
    cpu_state_.nb_tlb     = cpu_cfg.TlbSize();
    cpu_state_.tlb_in_use = cpu_state_.nb_tlb;
    cpu_state_.min_page_shift = cpu_cfg.MinPageShift();
    cpu_state_.phys_addr_mask = cpu_cfg.PhysAddrMask();
    device_ip_mask_           = cpu_cfg.DeviceIpMask();

    /* MIPS IV integer ops (MOVZ/MOVN/PREF) are present only on a kMips4 core; the
       decoder gates them on this flag and raises Reserved for them otherwise. */
    const bool has_64bit = cpu_cfg.IsaLevel() != MipsIsaLevel::kMips1;
    const bool has_eret  = cpu_cfg.IsaLevel() != MipsIsaLevel::kMips1;

    /* The MIPS I exception return, complementary to has_eret above. */
    const bool has_rfe   = cpu_cfg.IsaLevel() == MipsIsaLevel::kMips1;
    decoder_.Configure(cpu_cfg.HasFpu(), cpu_cfg.HasLlsc(),
                       cpu_cfg.IsaLevel() == MipsIsaLevel::kMips4,
                       cpu_cfg.HasVr41xxPowerModes(),
                       has_64bit, has_eret, has_rfe);

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

    LOG(Jit, "MipsJit::OnReady: bringup done; entry VA=0x%08X %d DRAM bank(s), "
             "%u SMC-indexed pages\n",
        cpu_state_.pc, dram_region_count_, dram_index_size_ >> 12);
}

void* MipsJit::JitCompile(uint32_t guest_pc) {
    if (guest_pc & 0x3u) {
        /* Unaligned instruction fetch -> AdEL (MIPS PC must be word-aligned). CE's
           PSL implicit-call trap relies on it: coredll jumps to an unaligned magic
           VA and the kernel decodes the fault EPC into the API method. */
        DeliverFetchAddressError(guest_pc);
        return nullptr;
    }

    uint32_t pa0 = 0;
    const MipsTlbResult fr =
        mmu_->Translate(&cpu_state_, guest_pc, MipsAccess::kFetch, &pa0);
    if (fr != MipsTlbResult::kMatch) {
        /* Instruction-fetch TLB miss/invalid: deliver TLBL so the guest's refill
           handler maps the page, then bail - Run re-dispatches at the vector. */
        DeliverFetchTlbException(guest_pc, fr);
        return nullptr;
    }
    /* Block phys-identity + decode span are bounded to the SoC minimum page
       (1<<min_page_shift): a block that crossed a min-page boundary would have a
       phys_start that only pins its first page, so an independent remap of a later
       page would go undetected (invisible at 4 KB / VR5500; real at 1 KB / VR4102). */
    const uint32_t page_off_mask = (1u << cpu_state_.min_page_shift) - 1u;
    block_ctx_.block_phys_page_base = pa0 & ~page_off_mask;
    const uint32_t phys_start = block_ctx_.block_phys_page_base | (guest_pc & page_off_mask);

    /* Global (kseg / G=1) blocks live in the shared `global` index; per-process
       (G=0) blocks in per_asid[ASID]. */
    const uint8_t asid = static_cast<uint8_t>(cpu_state_.cp0_entryhi & 0xFFu);
    const bool outer_global = mmu_->ExecPageGlobal(&cpu_state_, guest_pc);
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
    blocks_.IndexInsert(stored, &idx, BlockIndexKey(phys_start));
    if (!outer_global) blocks_.MarkPopulated(asid);

    const size_t code_size = JitGenerateCode(code, 1);
    arena_.FreeUnusedTail(code + code_size);
    stored->native_end = code + code_size;

    blocks_.JumpCacheInsert(guest_pc, stored->native_start);
    return stored->native_start;
}

void MipsJit::JitDecode(uint32_t guest_pc) {
    guest_pc &= ~0x3u;
    const uint32_t page_off_mask = (1u << cpu_state_.min_page_shift) - 1u;
    const uint32_t page_end = (guest_pc & ~page_off_mask) + page_off_mask + 1u;
    std::memset(block_ctx_.insns, 0, sizeof(block_ctx_.insns));

    uint32_t i = 0;
    bool delay_pending = false;  /* prior insn was a branch; this is its delay slot */
    for (; i < kMaxMipsInsnPerBlock && guest_pc < page_end; ++i, guest_pc += 4) {
        MipsDecodedInsn& insn = block_ctx_.insns[i];

        uint32_t pa = 0;
        if (mmu_->Translate(&cpu_state_, guest_pc, MipsAccess::kFetch, &pa) !=
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
        if (insn.ends_block) {          /* ERET / HIBERNATE: no delay slot */
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
       reserved field, flush on an ASID change. mask = VPN2(VA[31:S+1]) | ASID. */
    MipsCpuState& s = jit->cpu_state_;
    const uint32_t kMask = MipsVpn2Mask(s.min_page_shift) | 0xFFu;
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
    /* Fold the INTC-driven device IRQ level into cp0_cause on this thread
       (cp0_cause is JIT-owned; only external_ip_ crosses threads). */
    cpu_state_.cp0_cause =
        (cpu_state_.cp0_cause & ~device_ip_mask_) |
        (external_ip_.load(std::memory_order_acquire) & device_ip_mask_);
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
    if (mmu_->Translate(&cpu_state_, va, MipsAccess::kRead, &pa) !=
        MipsTlbResult::kMatch) {
        return std::nullopt;
    }
    uint8_t* host = memory_->TryTranslate(pa);
    if (!host) return std::nullopt;
    return host;
}

uint8_t* MipsJit::ResolveGuestVaToHost(uint32_t va) {
    uint32_t pa = 0;
    if (mmu_->Translate(&cpu_state_, va, MipsAccess::kRead, &pa) !=
        MipsTlbResult::kMatch) {
        return nullptr;
    }
    uint8_t* w = memory_->TryTranslateWrite(pa);
    return w ? w : memory_->TryTranslate(pa);
}

bool MipsJit::ResolveGuestVaToPa(uint32_t va, uint32_t* pa) {
    return mmu_->Translate(&cpu_state_, va, MipsAccess::kRead, pa) ==
           MipsTlbResult::kMatch;
}

uint32_t MipsJit::BlockIndexKey(uint32_t phys_start) {
    uint8_t* host = memory_->TryTranslateWrite(phys_start);
    if (!host) return kBlockUnindexed;

    const uint32_t off = DramIndexOffset(host);
    if (off != UINT32_MAX) return off;
    if (InInjectionBand(host)) return kBlockUnindexed;
    LOG(Caution, "MipsJit::BlockIndexKey: block at pa=0x%08X pc=0x%08X is in a "
            "writable region that is neither DRAM nor the injection band; a store "
            "into it would not invalidate the block\n", phys_start, cpu_state_.pc);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void MipsJit::InvalidateOnRamWrite(uint8_t* host, uint32_t size) {
    const uint32_t off = DramIndexOffset(host);
    if (off == UINT32_MAX) {
        if (InInjectionBand(host)) return;
        if (InDmaRegion(host)) return;
        LOG(Caution, "MipsJit::InvalidateOnRamWrite: store of %u byte(s) at host %p "
                "pc=0x%08X lands in a writable region that is neither DRAM nor the "
                "injection band; self-modifying code there is unmodeled\n",
                size, host, cpu_state_.pc);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (!blocks_.PageHasBlocks(off)) return;
    blocks_.RemoveRange(off, off + size - 1u);
}

void __fastcall MipsJit::TlbwiHelper(MipsJit* jit) {
    jit->mmu_->WriteIndexed(&jit->cpu_state_);
}

void __fastcall MipsJit::TlbwrHelper(MipsJit* jit) {
    jit->mmu_->WriteRandom(&jit->cpu_state_);
}

void __fastcall MipsJit::TlbpHelper(MipsJit* jit) {
    jit->mmu_->Probe(&jit->cpu_state_);
}

void __fastcall MipsJit::TlbrHelper(MipsJit* jit) {
    jit->mmu_->Read(&jit->cpu_state_);
}

uint32_t __fastcall MipsJit::Mfc0RandomHelper(MipsJit* jit) {
    return jit->mmu_->RandomIndex(&jit->cpu_state_);
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
