#pragma once

#include <cstdint>
#include <mutex>

#include "../core/service.h"
#include "arm_cpu.h"
#include "arm_jit_types.h"
#include "block_context.h"
#include "cpu_state.h"
#include "jit_block_index.h"
#include "jit_code_arena.h"

class ArmDecoder;
class ArmMmu;
struct ArmMmuState;
class ArmProcessorConfig;
class CoprocEmitter;
class EmulatedMemory;

class ArmJit : public Service {
public:
    using Service::Service;
    ~ArmJit() override;

    void OnReady() override;

    ArmCpuState* CpuState() { return cpu_->State(); }

    ArmCpu* Cpu() { return cpu_; }

    void* FindBlockNativeStart(uint32_t guest_pc);

    void Run();

    /* MUST establish ESI = ArmCpuState* and EBX = ArmMmuState*
       before CALLing ECX — every Place fn emit addresses CPU/MMU
       fields off these pinned bases and reading anything else
       reads wrong state. */
    static void __cdecl Dispatch(void*        native_pc,
                                 ArmCpuState* state,
                                 ArmMmuState* mmu_state);

    ArmMmu* Mmu() { return mmu_; }

    class PeripheralDispatcher* Peripheral() { return peripheral_; }

    ArmProcessorConfig* ProcessorConfig() { return processor_config_; }

    CoprocEmitter* Coproc() { return coproc_emitter_; }

    ArmDecoder* Decoder() { return decoder_; }

    class ArmVfp* Vfp() { return vfp_; }

    uint32_t* LdrUnalignedGuestAddressPtr() {
        return &ldr_unaligned_guest_address_;
    }

    uint32_t* EndEffectiveAddressPtr()    { return &end_effective_address_; }
    uint32_t* BaseAbortValuePtr()         { return &base_abort_value_; }
    uint32_t* StartPageHostAddressEndPtr(){ return &start_page_host_address_end_; }
    uint32_t* StartIoAddressPtr()         { return &start_io_address_; }
    uint32_t* StartPageIoAddressEndPtr()  { return &start_page_io_address_end_; }
    uint32_t* NextPageHostAddressPtr()    { return &next_page_host_address_; }
    uint32_t* NextPageIoAddressPtr()      { return &next_page_io_address_; }

    /* Cleared on every JitCodeArena flush — cached host pointers
       become stale the moment the arena reuses their slabs. */
    void* NativeAddr(ExceptionVector v) const {
        return native_addrs_[static_cast<uint32_t>(v)];
    }
    void SetNativeAddr(ExceptionVector v, void* p) {
        native_addrs_[static_cast<uint32_t>(v)] = p;
    }
    void FlushNativeAddrCache() {
        for (auto& a : native_addrs_) a = nullptr;
    }

    std::mutex& InterruptLock() { return interrupt_lock_; }

    void SignalIdleWake();

    void* IdleEvent() const { return idle_event_; }

    /* Caller MUST hold InterruptLock — the body try_locks + aborts
       if not held, since concurrent peripheral threads racing the
       trampoline-byte patch produces torn (cpsr, byte) state. */
    void UpdateInterruptOnPoll();

    /* SetEvent fires unconditionally outside the lock: conditional
       wake races CPSR.I clear from JIT thread and parks the
       dispatcher with an undelivered pending IRQ. */
    void SetInterruptPending();

    void ClearInterruptPending();

    void SetResetPending();

    /* JIT-emitted MCR p15 c15 c2 op_2=2 (SA-1110 Wait-For-Interrupt)
       calls this. Blocks on idle_event_ until a peripheral asserts an
       IRQ. Advances guest_cycle_counter by wallclock-elapsed*divider so
       OEMIdle's post-WFI OSCR read sees time having passed. */
    static void __fastcall WfiHelper(ArmJit* jit);

    /* __fastcall: ECX = va, EDX = tlb_hint, stack = jit. Nullptr
       return + io_pending_address_ set ⇒ peripheral I/O dispatch;
       nullptr without io_pending set ⇒ data abort. */
    static uint8_t* __fastcall TranslateReadHelper     (uint32_t va, int8_t* hint, ArmJit* jit);
    static uint8_t* __fastcall TranslateWriteHelper    (uint32_t va, int8_t* hint, ArmJit* jit);
    static uint8_t* __fastcall TranslateReadWriteHelper(uint32_t va, int8_t* hint, ArmJit* jit);

    static uint8_t* __fastcall MapGuestPhysicalToHostRamHelper(uint32_t paddr,
                                                               ArmJit*  jit);
    static uint8_t* __fastcall MapGuestPhysicalToHostHelper(uint32_t paddr,
                                                            ArmJit*  jit);

    static void __cdecl RaiseAlignmentExceptionHelper(ArmJit* jit, uint32_t va);

    static void* __cdecl FindBlockNativeStartHelper(ArmJit*  jit,
                                                    uint32_t guest_pc);

    /* One-byte naked RET — shadow-stack slots for "not yet jitted"
       return addresses point here so the eventual BX LR / MOV PC,LR
       RETs back to the dispatcher rather than JMPing into garbage. */
    static void NotJittedHelper();

    /* Self-modifying: patches [ESI] in place to JMP rel32 to the
       resolved entrypoint's native_start, FlushInstructionCache's
       the bytes, then JMPs back to ESI. */
    static void EntrypointEndHelper();

    static JitBlock* __cdecl FindBlockExactHelper(ArmJit*  jit,
                                                  uint32_t guest_pc);

    /* __fastcall: ECX = RegisterList, EDX = jit. */
    static void __fastcall BlockDataTransferIOLoadHelper(uint32_t register_list, ArmJit* jit);
    static void __fastcall BlockDataTransferIOStoreHelper(uint32_t register_list, ArmJit* jit);

    /* __fastcall: ECX = RegisterListAndFlags, EDX = jit, stack =
       instruction_address (only used when R15 is in the list). */
    static void __fastcall BlockDataTransferIOHelperSlow(uint32_t register_list_and_flags,
                                                          ArmJit*  jit,
                                                          uint32_t instruction_address);

    /* (0, 0xFFFFFFFF) = whole-cache flush. va/length are widened
       to per-SoC cache-line size before the range-overlap check. */
    void FlushTranslationCache(uint32_t va, uint32_t length);

    static void __cdecl FlushTranslationCacheStaticHelper(ArmJit*  jit,
                                                          uint32_t va,
                                                          uint32_t length);

    void* FlushTranslationCacheTrampoline() {
        return flush_translation_cache_helper_;
    }

private:
    JitCodeArena    arena_;
    JitBlockIndex   blocks_arm_;
    JitBlockIndex   blocks_thumb_;
    BlockContext    block_ctx_{};

    ArmCpu*                       cpu_              = nullptr;
    ArmMmu*                       mmu_              = nullptr;
    EmulatedMemory*               memory_           = nullptr;
    class PeripheralDispatcher*   peripheral_       = nullptr;
    ArmProcessorConfig*           processor_config_ = nullptr;
    CoprocEmitter*                coproc_emitter_   = nullptr;
    ArmDecoder*                   decoder_          = nullptr;
    class ArmVfp*                 vfp_              = nullptr;

    uint32_t        ldr_unaligned_guest_address_ = 0;

    uint32_t        end_effective_address_         = 0;
    uint32_t        base_abort_value_              = 0;
    uint32_t        start_page_host_address_end_   = 0;
    uint32_t        start_io_address_              = 0;
    uint32_t        start_page_io_address_end_     = 0;
    uint32_t        next_page_host_address_        = 0;
    uint32_t        next_page_io_address_          = 0;

    void*           native_addrs_[static_cast<uint32_t>(ExceptionVector::kCount)] = {nullptr};

    uint8_t*        interrupt_check_                = nullptr;
    uint8_t*        r15_modified_helper_            = nullptr;
    uint8_t*        branch_helper_                  = nullptr;
    uint8_t*        shadow_stack_helper_            = nullptr;
    uint8_t*        pop_shadow_stack_helper_        = nullptr;
    uint8_t*        raise_abort_data_helper_        = nullptr;
    uint8_t*        block_usermode_helper_          = nullptr;
    uint8_t*        flush_translation_cache_helper_ = nullptr;

    void*           idle_event_ = nullptr;

    std::mutex      interrupt_lock_;

    /* JIT thread reads/writes shadow_stack_ without locking;
       cross-thread access would race. */
    ShadowStackEntry shadow_stack_[256]{};
    uint8_t          shadow_stack_count_ = 0;

    void SetInterruptPendingLocked();

#if CERF_DEV_MODE
    static void __cdecl TraceDispatchPcHelper(ArmJit* jit, uint32_t pc, ArmCpuState* state);
#endif
    void InitializeInterruptCheck();
    void InitializeR15ModifiedHelper();
    void InitializeBranchHelper();
    void InitializeShadowStackHelper();
    void InitializePopShadowStackHelper();
    void InitializeRaiseAbortDataHelper();
    void InitializeBlockUsermodeHelper();
    void InitializeFlushTranslationCacheHelper();

    void* JitCompile(uint32_t guest_pc);

    void JitDecode(JitBlock* containing_block, uint32_t guest_pc);

    int JitOptimizeIR();

    int LocateEntrypoints();

    void JitCreateEntrypoints(JitBlock* containing_block,
                              uint8_t*  prefix_slab);

    void OptimizeARMFlags();

    size_t JitGenerateCode(uint8_t* code_location, int entrypoint_count);

    void JitApplyFixups();

};
