#pragma once

#include <cstdint>
#include <mutex>

#include "../../core/service.h"
#include "../jit_code_arena.h"
#include "../isa_block_space.h"
#include "mips_block_context.h"
#include "mips_cpu_state.h"
#include "mips_decoder.h"
#include "mips_mmu.h"

class EmulatedMemory;
class PeripheralDispatcher;

class MipsJit : public Service {
public:
    using Service::Service;
    ~MipsJit() override;

    void OnReady() override;

    MipsCpuState* CpuState() { return &cpu_state_; }

    /* Establish ESI = MipsCpuState* before CALLing native_pc; every emitted
       block addresses GPR/CP0/TLB fields off ESI. */
    static void __cdecl Dispatch(void* native_pc, MipsCpuState* state);

    /* Emitted by PlaceMipsUndefined for an opcode the decoder rejected or a
       place fn not yet implemented: logs op + PC, then CerfFatalExit (never a
       silent UND - an unimplemented MIPS path must surface loudly). */
    static void __cdecl UnimplementedHelper(MipsJit* jit, uint32_t pc, uint32_t raw);

    void  Run();
    void* FindBlockNativeStart(uint32_t guest_pc);

    /* __fastcall: ECX = va, EDX = jit. Software-TLB translate behind the
       emitted inline fast probe; nullptr + io_pending set => peripheral I/O,
       nullptr without it => the CP0 TLB/address exception was raised. */
    static uint8_t* __fastcall TranslateReadHelper (uint32_t va, MipsJit* jit);
    static uint8_t* __fastcall TranslateWriteHelper(uint32_t va, MipsJit* jit);
    static uint8_t* __fastcall TranslateFetchHelper(uint32_t va, MipsJit* jit);

    MipsMmu*     Mmu()        { return &mmu_; }
    MipsDecoder* Decoder()    { return &decoder_; }
    PeripheralDispatcher* Peripheral() { return peripheral_; }

    std::mutex& InterruptLock() { return interrupt_lock_; }
    void SetInterruptPending();
    void ClearInterruptPending();
    void SignalIdleWake();
    void* IdleEvent() const { return idle_event_; }

    void SetResetPending(bool is_resume = false);
    void NotifyResetDelivered();

    void FlushTranslationCache(uint32_t va, uint32_t length);

private:
    JitCodeArena    arena_;
    IsaBlockSpace   blocks_;          /* single ISA - no ARM/Thumb split */
    MipsBlockContext block_ctx_{};
    MipsCpuState    cpu_state_{};
    MipsMmu         mmu_;
    MipsDecoder     decoder_;

    EmulatedMemory*       memory_     = nullptr;
    PeripheralDispatcher* peripheral_ = nullptr;

    void*       idle_event_ = nullptr;
    std::mutex  interrupt_lock_;
    uint8_t*    interrupt_check_ = nullptr;
    uint8_t*    branch_helper_   = nullptr;
    bool        tc_flush_pending_ = false;

    void* JitCompile(uint32_t guest_pc);
    void  JitDecode(uint32_t guest_pc);
    int   LocateEntrypoints();
    void  JitCreateEntrypoints(JitBlock* containing_block, uint8_t* prefix_slab);
    size_t JitGenerateCode(uint8_t* code_location, int entrypoint_count);
    void  JitApplyFixups();
};
