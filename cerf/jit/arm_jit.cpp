#include "arm_jit.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <intrin.h>

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../cpu/arm_processor_config.h"
#include "../cpu/emulated_memory.h"
#include "../peripherals/peripheral_dispatcher.h"
#include "../boot/rom_parser_service.h"
#include "../socs/page_table_builder.h"
#include "arm_cp15_sctlr_handler.h"
#include "arm_cpu.h"
#include "arm_cpu_exceptions.h"
#include "arm_cpu_ops.h"
#include "arm_decoder.h"
#include "arm_jit_runtime.h"
#include "arm_mmu.h"
#include "arm_vfp.h"
#include "arm_mmu_state.h"
#include "coproc_emitter.h"
#include "place_fns.h"
#include "x86_emit.h"

REGISTER_SERVICE(ArmJit);

ArmJit::~ArmJit() {
    if (interrupt_check_) {
        VirtualFree(interrupt_check_, 0, MEM_RELEASE);
        interrupt_check_ = nullptr;
    }
    if (r15_modified_helper_) {
        VirtualFree(r15_modified_helper_, 0, MEM_RELEASE);
        r15_modified_helper_ = nullptr;
    }
    if (branch_helper_) {
        VirtualFree(branch_helper_, 0, MEM_RELEASE);
        branch_helper_ = nullptr;
    }
    if (shadow_stack_helper_) {
        VirtualFree(shadow_stack_helper_, 0, MEM_RELEASE);
        shadow_stack_helper_ = nullptr;
    }
    if (pop_shadow_stack_helper_) {
        VirtualFree(pop_shadow_stack_helper_, 0, MEM_RELEASE);
        pop_shadow_stack_helper_ = nullptr;
    }
    if (raise_abort_data_helper_) {
        VirtualFree(raise_abort_data_helper_, 0, MEM_RELEASE);
        raise_abort_data_helper_ = nullptr;
    }
    if (block_usermode_helper_) {
        VirtualFree(block_usermode_helper_, 0, MEM_RELEASE);
        block_usermode_helper_ = nullptr;
    }
    if (flush_translation_cache_helper_) {
        VirtualFree(flush_translation_cache_helper_, 0, MEM_RELEASE);
        flush_translation_cache_helper_ = nullptr;
    }
    if (idle_event_) {
        CloseHandle(idle_event_);
        idle_event_ = nullptr;
    }
}

void ArmJit::SignalIdleWake() {
    if (idle_event_) {
        SetEvent(idle_event_);
    }
}

__declspec(naked) void ArmJit::NotJittedHelper() {
    /* MUST stay a single RET — shadow-stack slots for "not yet
       jitted" return targets point here; JMP-to-anything-else
       breaks the BX LR / MOV PC, LR return path. */
    __asm { ret }
}

void ArmJit::OnReady() {
    LOG(Jit, "ArmJit::OnReady: resolving dependencies\n");
    cpu_              = &emu_.Get<ArmCpu>();
    mmu_              = &emu_.Get<ArmMmu>();
    memory_           = &emu_.Get<EmulatedMemory>();
    peripheral_       = &emu_.Get<PeripheralDispatcher>();
    processor_config_ = &emu_.Get<ArmProcessorConfig>();
    coproc_emitter_   = &emu_.Get<CoprocEmitter>();
    decoder_          = &emu_.Get<ArmDecoder>();
    vfp_              = &emu_.Get<ArmVfp>();

    /* Resolving ArmJit inside ArmCpu::OnReady would form a
       service-locator cycle — back-pointer wired here instead. */
    cpu_->LateInit(this);

    arena_.Initialize();
    blocks_arm_  .Initialize();
    blocks_thumb_.Initialize();
    InitializeInterruptCheck();
    InitializeR15ModifiedHelper();
    InitializeBranchHelper();
    InitializeShadowStackHelper();
    InitializePopShadowStackHelper();
    InitializeRaiseAbortDataHelper();
    InitializeBlockUsermodeHelper();
    InitializeFlushTranslationCacheHelper();

    /* Auto-reset event, initially non-signaled. Matches the
       hIdleEvent = CreateEvent(NULL, FALSE, FALSE, NULL). */
    idle_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!idle_event_) {
        LOG(Caution, "ArmJit: CreateEventW(idle_event) failed gle=%lu\n",
            GetLastError());
        CerfFatalExit(2);
    }

    block_ctx_.jit                        = this;
    block_ctx_.interrupt_check_target     = interrupt_check_;
    block_ctx_.r15_modified_helper_target = r15_modified_helper_;
    block_ctx_.branch_helper_target       = branch_helper_;
    block_ctx_.shadow_stack_helper_target     = shadow_stack_helper_;
    block_ctx_.pop_shadow_stack_helper_target = pop_shadow_stack_helper_;
    block_ctx_.raise_abort_data_helper_target = raise_abort_data_helper_;
    block_ctx_.block_usermode_helper_target   = block_usermode_helper_;

    /* SCTLR-write trampoline lives on the ArmCp15SctlrHandler
       service. The handler can't reach ArmJit at its own OnReady
       (would form a service-locator cycle); ArmJit drives the
       wire-up here once both services are resolved. */
    auto& sctlr_handler = emu_.Get<ArmCp15SctlrHandler>();
    sctlr_handler.InitializeTrampoline(this);
    block_ctx_.sctlr_write_target =
        static_cast<uint8_t*>(sctlr_handler.Trampoline());

    auto& page_tables = emu_.Get<PageTableBuilder>();
    auto& rom         = emu_.Get<RomParserService>();
    /* MMU off at cold reset — PC is consumed as PA, so feed entry_pa
       (OAT-translated) not entry_va; high-VA delivered as PC faults. */
    const uint32_t sp_pa     = page_tables.InitStackTopPa();
    const uint32_t entry_va  = rom.EntryVa();
    const uint32_t entry_pa  = page_tables.VaToPa(entry_va);
    cpu_->SetInitialStackPointer(sp_pa);
    cpu_->RaiseResetException(entry_pa);
    cpu_->BankSwitch();
    LOG(Jit, "ArmJit::OnReady: bringup done; SP=0x%08X "
              "entry_va=0x%08X entry_pa=0x%08X CPSR=0x%08X "
              "R15=0x%08X R13=0x%08X\n",
        sp_pa, entry_va, entry_pa, cpu_->GetCpsrWithFlags(),
        cpu_->State()->gprs[15], cpu_->State()->gprs[13]);
}

void ArmJit::UpdateInterruptOnPoll() {
    /* Caller must hold InterruptLock — concurrent peripheral
       threads racing here without it corrupt the trampoline byte. */
    if (interrupt_lock_.try_lock()) {
        interrupt_lock_.unlock();
        LOG(Caution, "ArmJit::UpdateInterruptOnPoll called without InterruptLock\n");
        CerfFatalExit(2);
    }

    const ArmCpuState* state = cpu_->State();
    const bool deliver = !state->cpsr.bits.irq_disable
                      && state->irq_interrupt_pending != 0;


    /* Target byte: 0x90 NOP falls through into the IRQ-delivery body;
       0xC3 RETN returns the JIT-emitted CALL immediately. */
    const uint8_t target = deliver ? 0x90u : 0xC3u;

    /* No FlushInstructionCache: x86 I-cache is MESI-coherent across
       cores; FIC here issues a process-wide IPI not needed for
       cross-thread byte writes. */
    interrupt_check_[0] = target;
}

void ArmJit::SetInterruptPendingLocked() {
    cpu_->State()->irq_interrupt_pending = 1;
    UpdateInterruptOnPoll();
}

void ArmJit::SetInterruptPending() {
    {
        std::lock_guard<std::mutex> guard(interrupt_lock_);
        SetInterruptPendingLocked();
    }
    SetEvent(idle_event_);
}

void ArmJit::ClearInterruptPending() {
    std::lock_guard<std::mutex> guard(interrupt_lock_);
    cpu_->State()->irq_interrupt_pending = 0;
    UpdateInterruptOnPoll();
}

void __fastcall ArmJit::WfiHelper(ArmJit* jit) {
    auto* state = jit->cpu_->State();
    if (state->irq_interrupt_pending != 0 || state->reset_pending != 0) return;
    const auto start = std::chrono::steady_clock::now();
    /* 1 ms cap: if no IRQ source is enabled (early boot before OEMInit
       wires INTC), WFI would block forever. Kernel re-enters WFI in a
       loop until time advances; bounded wait keeps that loop liveness. */
    WaitForSingleObject(jit->idle_event_, 1);
    const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    const uint32_t divider = jit->ProcessorConfig()->CpuToOscrDivider();
    const uint64_t oscr_ticks = (static_cast<uint64_t>(elapsed_ns) * 3686400ull) / 1000000000ull;
    state->guest_cycle_counter += static_cast<uint32_t>(oscr_ticks * divider);
}

void ArmJit::SetResetPending() {
    ArmCpuState* state = cpu_->State();
    state->spsr.bits.irq_disable = 0;
    state->reset_pending          = 1;
    {
        std::lock_guard<std::mutex> guard(interrupt_lock_);
        state->cpsr.bits.irq_disable = 0;
        SetInterruptPendingLocked();
    }
    SetEvent(idle_event_);
}

void* ArmJit::FindBlockNativeStart(uint32_t guest_pc) {
    /* FCSE fold — the block index is keyed on post-fold guest VAs
       so two processes' low-32-MB code never aliases. */
    const uint32_t folded = ApplyFcseFold(*mmu_->State(), guest_pc);

    JitBlockIndex& idx = cpu_->State()->cpsr.bits.thumb_mode ? blocks_thumb_ : blocks_arm_;
    JitBlock* block = idx.FindExact(folded);
    if (!block) return nullptr;
    return block->native_start;
}

/* Filter for the __except below: log the fault context (host EIP +
   guest state) before the handler runs CerfFatalExit. Lives here so
   GetExceptionInformation() is called at the top of a filter
   expression — MSVC rejects it inside nested expressions. */
static int DispatchFaultFilter(EXCEPTION_POINTERS* xp,
                               uint32_t guest_pc, void* native,
                               ArmCpuState* state) {
    /* Best-effort symbol resolution. SymInitialize against the
       running process loads cerf.pdb automatically when present
       next to cerf.exe. */
    static bool dbghelp_inited = false;
    if (!dbghelp_inited) {
        SymInitialize(GetCurrentProcess(), nullptr, TRUE);
        dbghelp_inited = true;
    }
    char sym_buf[sizeof(SYMBOL_INFO) + 512] = {0};
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(sym_buf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen   = 512;
    DWORD64 disp = 0;
    const DWORD64 addr = reinterpret_cast<DWORD64>(
        xp->ExceptionRecord->ExceptionAddress);
    const bool resolved =
        SymFromAddr(GetCurrentProcess(), addr, &disp, sym) != 0;

    const CONTEXT* ctx = xp->ContextRecord;

    const auto np = xp->ExceptionRecord->NumberParameters;
    const auto fault_kind = np >= 1 ? xp->ExceptionRecord->ExceptionInformation[0] : 0;
    const auto fault_addr = np >= 2 ? xp->ExceptionRecord->ExceptionInformation[1] : 0;
    /* Dump 128 bytes (64 before + 64 after) of JIT-emitted code
       around the fault EIP for emit-pattern context. */
    char hex[128 * 3 + 16] = {0};
    char* hp = hex;
    const uint8_t* base = reinterpret_cast<const uint8_t*>(
        xp->ExceptionRecord->ExceptionAddress) - 64;
    for (int i = 0; i < 128; ++i) {
        const uint8_t* p = base + i;
        const uint8_t b = (IsBadReadPtr(p, 1) == FALSE) ? *p : 0;
        hp += sprintf_s(hp, 4, "%02X ", b);
    }
    LOG(Caution, "ArmJit::Run fault EIP -64..+64: %s (^ fault EIP at offset 64)\n", hex);
    LOG(Caution,
        "ArmJit::Run: JIT-emitted code faulted code=0x%08X kind=%lu addr=0x%08lX "
        "native_eip=%p (%s+0x%llX) guest_pc=0x%08X dispatched_native=%p\n"
        "  Host EAX=0x%08X ECX=0x%08X EDX=0x%08X EBX=0x%08X "
        "ESP=0x%08X EBP=0x%08X ESI=0x%08X EDI=0x%08X\n"
        "  R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X "
        "R4=0x%08X R5=0x%08X R6=0x%08X R7=0x%08X\n"
        "  R8=0x%08X R9=0x%08X R10=0x%08X R11=0x%08X "
        "R12=0x%08X SP=0x%08X LR=0x%08X PC=0x%08X CPSR=0x%08X\n",
        xp->ExceptionRecord->ExceptionCode,
        static_cast<unsigned long>(fault_kind),
        static_cast<unsigned long>(fault_addr),
        xp->ExceptionRecord->ExceptionAddress,
        resolved ? sym->Name : "<unresolved>",
        (unsigned long long)(resolved ? disp : 0),
        guest_pc, native,
        ctx->Eax, ctx->Ecx, ctx->Edx, ctx->Ebx,
        ctx->Esp, ctx->Ebp, ctx->Esi, ctx->Edi,
        state->gprs[0],  state->gprs[1],  state->gprs[2],  state->gprs[3],
        state->gprs[4],  state->gprs[5],  state->gprs[6],  state->gprs[7],
        state->gprs[8],  state->gprs[9],  state->gprs[10], state->gprs[11],
        state->gprs[12], state->gprs[13], state->gprs[14], state->gprs[15],
        ArmCpuGetCpsrWithFlags(state));
    return EXCEPTION_EXECUTE_HANDLER;
}

void ArmJit::Run() {
    ArmCpuState* state = cpu_->State();
    const uint32_t pc = state->gprs[15];
    void* native = FindBlockNativeStart(pc);
    if (!native) {
        native = JitCompile(pc);
    }
    __try {
        Dispatch(native, state, mmu_->State());
    }
    __except (DispatchFaultFilter(GetExceptionInformation(), pc, native, state)) {
        CerfFatalExit(2);
    }
}

__declspec(naked) void __cdecl ArmJit::Dispatch(void*        /* native_pc */,
                                                ArmCpuState* /* state */,
                                                ArmMmuState* /* mmu_state */) {
    /* MUST preserve EBP/EBX (and EDI/ESI for symmetry): caller is
       normal C++ with frame pointer, [ESP+20/24/28] are the original
       args after the 4 PUSHes — adding work before the PUSHes shifts
       the offsets and the block reads wrong stack slots. */
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov  ecx, [esp + 20]
        mov  esi, [esp + 24]
        mov  ebx, [esp + 28]
        call ecx
        pop  edi
        pop  esi
        pop  ebx
        pop  ebp
        ret
    }
}

JitBlock* __cdecl ArmJit::FindBlockExactHelper(ArmJit*  jit,
                                               uint32_t guest_pc) {
    /* FCSE fold matches FindBlockNativeStart's behavior (so a
       sub-32-MB guest_pc with the kernel's process_id active maps
       to the same key the index was inserted with). */
    const uint32_t folded = ApplyFcseFold(*jit->mmu_->State(), guest_pc);
    JitBlockIndex& idx =
        jit->CpuState()->cpsr.bits.thumb_mode ? jit->blocks_thumb_ : jit->blocks_arm_;
    return idx.FindExact(folded);
}

__declspec(naked) void ArmJit::EntrypointEndHelper() {
    /* EDI = patch slot, EAX = JitBlock* on entry. EDI is
       callee-saved across __stdcall FlushInstructionCache; ESI must
       stay untouched so the JIT's pinned ArmCpuState* survives. */
    __asm {
        mov eax, [eax + 8]                  ; EAX = JitBlock::native_start
        mov byte ptr [edi], 0xE9            ; opcode: JMP rel32
        sub eax, edi
        sub eax, 5                          ; EAX = native_start - (EDI + 5) = rel32
        mov dword ptr [edi + 1], eax        ; store rel32

        push 5                              ; arg3: size
        push edi                            ; arg2: BaseAddress
        push 0xFFFFFFFF                     ; arg1: hProcess (-1 = GetCurrentProcess())
        call FlushInstructionCache          ; __stdcall, callee pops 12 bytes

        jmp edi                             ; execute the patched JMP rel32
    }
}

