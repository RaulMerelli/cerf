#pragma once

#include <cstdint>
#include <mutex>

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../guest_engine.h"
#include "../jit_code_arena.h"
#include "../isa_block_space.h"
#include "mips_block_context.h"
#include "mips_cpu_state.h"
#include "mips_decoder.h"
#include "mips_mmu.h"

class EmulatedMemory;
class PeripheralDispatcher;
class MipsProcessorConfig;

class MipsJit : public GuestEngine {
public:
    using GuestEngine::GuestEngine;
    ~MipsJit() override;

    void OnReady() override;
    bool ShouldRegister() override {
        return emu_.Get<BoardDetector>().GetCpuArch() == CpuArch::Mips;
    }

    MipsCpuState* CpuState() { return &cpu_state_; }

    /* Establish ESI = MipsCpuState* before CALLing native_pc; every emitted
       block addresses GPR/CP0/TLB fields off ESI. */
    static void __cdecl Dispatch(void* native_pc, MipsCpuState* state);

    /* Emitted by PlaceMipsUndefined for an opcode the decoder rejected or a
       place fn not yet implemented: logs op + PC, then CerfFatalExit (never a
       silent UND - an unimplemented MIPS path must surface loudly). */
    static void __cdecl UnimplementedHelper(MipsJit* jit, uint32_t pc, uint32_t raw);

    /* Emitted by trapping arithmetic (ADDI/ADD/SUB) on a signed overflow:
       loud-fatals until CP0 Integer Overflow exception delivery exists. */
    static void __cdecl ArithOverflowHelper(MipsJit* jit, uint32_t pc);

    /* TLBWI: drive the indexed software-TLB write (and its JIT block-cache
       invalidation) via MipsMmu::WriteIndexed. __fastcall: jit in ECX. */
    static void __fastcall TlbwiHelper(MipsJit* jit);

    /* After a delay slot: set pc from branch_state/btarget/bcond, clear it, and
       return 1 (resolved) / 0 (branch_state was kNone, so a normally-entered
       block's insn[0] continues). QEMU gen_branch. __fastcall: fallthrough VA in
       ECX, jit in EDX. */
    static int __fastcall ResolveBranchHelper(uint32_t fallthrough, MipsJit* jit);

    void     Run() override;
    bool     DeepSleep()    const override { return cpu_state_.deep_sleep != 0; }
    bool     ResetPending() const override { return cpu_state_.reset_pending != 0; }
    uint32_t Pc()           const override { return cpu_state_.pc; }
    void     DispatchTraceIter() override {}  /* no run-loop trace consumer for MIPS */

    void* FindBlockNativeStart(uint32_t guest_pc);

    /* MIPS memory access: per-width all-in-one helpers that translate the
       effective address (kseg fold or software TLB) and perform the access.
       __fastcall passes the VA in ECX, the store value in EDX, and the jit
       pointer on the stack. StoreWordHelper writes gpr[rt][31:0] to mem[EA]. */
    static void __fastcall StoreWordHelper(uint32_t va, uint32_t value, MipsJit* jit);

    /* StoreHalfHelper writes value[15:0] to mem[EA] (2-byte store, EA must be
       2-aligned). __fastcall: VA in ECX, value in EDX, jit on the stack. */
    static void __fastcall StoreHalfHelper(uint32_t va, uint32_t value, MipsJit* jit);

    /* StoreByteHelper writes value[7:0] to mem[EA] (bytes are always aligned).
       __fastcall: VA in ECX, value in EDX, jit on the stack. */
    static void __fastcall StoreByteHelper(uint32_t va, uint32_t value, MipsJit* jit);

    /* LoadWordHelper returns mem[EA] (32-bit) in EAX; the place fn sign-extends
       it into the 64-bit rt. __fastcall: VA in ECX, jit in EDX. */
    static uint32_t __fastcall LoadWordHelper(uint32_t va, MipsJit* jit);

    /* LoadByteHelper returns mem[EA] (the raw byte) zero-extended in EAX; the
       place fn sign- or zero-extends per LB/LBU. Bytes are always aligned.
       __fastcall: VA in ECX, jit in EDX. */
    static uint32_t __fastcall LoadByteHelper(uint32_t va, MipsJit* jit);

    /* LoadHalfHelper returns mem[EA] (the raw 16-bit halfword) zero-extended in
       EAX; the place fn sign- or zero-extends per LH/LHU. EA must be 2-aligned.
       __fastcall: VA in ECX, jit in EDX. */
    static uint32_t __fastcall LoadHalfHelper(uint32_t va, MipsJit* jit);

    /* LwlHelper: unaligned load-word-left - merge the aligned word at va&~3 into
       gpr[rt]'s high bytes (keep the low), sign-extend. __fastcall: VA=ECX,
       rt index=EDX, jit on stack. (QEMU translate.c gen_lxl + ext32s, OPC_LWL.) */
    static void __fastcall LwlHelper(uint32_t va, uint32_t rt, MipsJit* jit);

    /* StoreDwordHelper writes the full 64-bit gpr[rt] to mem[EA]. The register
       index (not the 64-bit value) is passed so the datum needn't cross the
       __fastcall ABI: VA in ECX, rt index in EDX, jit on the stack. */
    static void __fastcall StoreDwordHelper(uint32_t va, uint32_t rt, MipsJit* jit);

    /* SdrHelper: unaligned store-doubleword-right (little-endian). Stores the
       low (va&7)^7 + 1 bytes of gpr[rt] to mem[va..]. __fastcall: VA in ECX, rt
       index in EDX, jit on the stack. (QEMU tcg/ldst_helper.c helper_sdr.) */
    static void __fastcall SdrHelper(uint32_t va, uint32_t rt, MipsJit* jit);

    /* SdlHelper: unaligned store-doubleword-left (little-endian). Stores the
       high (va&7)+1 bytes of gpr[rt] to mem[va], mem[va-1], ... __fastcall: VA
       in ECX, rt index in EDX, jit on the stack. (QEMU helper_sdl.) */
    static void __fastcall SdlHelper(uint32_t va, uint32_t rt, MipsJit* jit);

    /* SwrHelper: unaligned store-word-right (little-endian). Stores the low
       ((va&3)^3)+1 bytes of gpr[rt][31:0] to mem[va..]. __fastcall: VA in ECX,
       rt index in EDX, jit on the stack. (QEMU helper_swr.) */
    static void __fastcall SwrHelper(uint32_t va, uint32_t rt, MipsJit* jit);

    /* SwlHelper: unaligned store-word-left (little-endian). Stores the high
       (va&3)+1 bytes of gpr[rt][31:0] to mem[va], mem[va-1], ... __fastcall: VA
       in ECX, rt index in EDX, jit on the stack. (QEMU helper_swl.) */
    static void __fastcall SwlHelper(uint32_t va, uint32_t rt, MipsJit* jit);

    /* LoadDwordHelper returns the 64-bit mem[EA] in EDX:EAX (the MSVC 64-bit
       return ABI); the place fn writes EAX->gpr[rt] low, EDX->high. __fastcall:
       VA in ECX, jit in EDX. */
    static uint64_t __fastcall LoadDwordHelper(uint32_t va, MipsJit* jit);

    MipsMmu*     Mmu()        { return &mmu_; }
    MipsDecoder* Decoder()    { return &decoder_; }
    PeripheralDispatcher* Peripheral() { return peripheral_; }
    MipsProcessorConfig*  CpuConfig()  { return cpu_config_; }

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

    EmulatedMemory*       memory_      = nullptr;
    PeripheralDispatcher* peripheral_  = nullptr;
    MipsProcessorConfig*  cpu_config_  = nullptr;

    void*       idle_event_ = nullptr;
    std::mutex  interrupt_lock_;
    uint8_t*    interrupt_check_ = nullptr;
    uint8_t*    branch_helper_   = nullptr;
    bool        tc_flush_pending_ = false;

    void* JitCompile(uint32_t guest_pc);
    void  JitDecode(uint32_t guest_pc);

    /* Translate one byte for write (kseg fold / TLB) and store it; loud-fatals
       on the not-yet-built CP0-exception / MMIO paths. Shared by the byte-wise
       unaligned stores (SDL/SDR). `who` tags the diagnostic. */
    static void StoreByteXlate(MipsJit* jit, uint32_t va, uint8_t value,
                               const char* who);

    /* Route a guest access whose PA is not RAM-backed to the peripheral MMIO
       dispatcher (width = 1/2/4 bytes); loud-fatal when no peripheral claims
       the PA (truly unmapped). `who` tags the diagnostic. */
    uint32_t MmioRead (uint32_t pa, uint32_t width, const char* who);
    void     MmioWrite(uint32_t pa, uint32_t value, uint32_t width, const char* who);
    int   LocateEntrypoints();
    void  JitCreateEntrypoints(JitBlock* containing_block, uint8_t* prefix_slab);
    size_t JitGenerateCode(uint8_t* code_location, int entrypoint_count);
    void  JitApplyFixups();
};
