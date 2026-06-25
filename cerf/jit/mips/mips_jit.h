#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../tracing/trace_manager.h"
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
class MipsCp0Emitter;

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

    /* TLBWR: write the TLB entry at a random index via MipsMmu::WriteRandom.
       __fastcall: jit in ECX. */
    static void __fastcall TlbwrHelper(MipsJit* jit);

    /* TLBP: probe the TLB for EntryHi, set CP0_Index via MipsMmu::Probe.
       __fastcall: jit in ECX. */
    static void __fastcall TlbpHelper(MipsJit* jit);

    /* TLBR: read the TLB entry indexed by CP0_Index into EntryHi/EntryLo0/EntryLo1/
       PageMask via MipsMmu::Read. __fastcall: jit in ECX. */
    static void __fastcall TlbrHelper(MipsJit* jit);

    /* MTC0 Count / Compare side effects (the in-core timer). Count re-anchors to
       the live guest cycle; Compare lowers the pending IP7 and re-arms the timer
       (QEMU cp0_timer.c store_count / store_compare). __fastcall: value in ECX,
       jit in EDX. */
    static void __fastcall Mtc0CountHelper(uint32_t value, MipsJit* jit);
    static void __fastcall Mtc0CompareHelper(uint32_t value, MipsJit* jit);

    /* MTC0/DMTC0 EntryHi write: store the value; on an ASID-field change flush
       the VA jump cache (QEMU helper_mtc0_entryhi -> tlb_flush, cp0_helper.c:1170).
       __fastcall: value in ECX, jit in EDX. */
    static void __fastcall Mtc0EntryHiHelper(uint32_t value, MipsJit* jit);

    /* ERET: return from exception - PC=ErrorEPC (if Status.ERL) else EPC, clear
       the corresponding level bit, clear LLbit (MIPS64 Vol2 ERET). __fastcall:
       jit in ECX. */
    static void __fastcall EretHelper(MipsJit* jit);

    /* SYSCALL / BREAK: deliver the Sys (cause 8) / Bp (cause 9) CP0 exception to
       the general vector; the guest handler processes it and ERETs past the
       instruction. __fastcall: jit in ECX. */
    static void __fastcall SyscallHelper(MipsJit* jit);
    static void __fastcall BreakHelper(MipsJit* jit);

    /* SEH code a guest CP0 exception raises from inside a memory/arith helper;
       Run()'s __except catches it and resumes at the (already-set) vector PC.
       Customer-defined NTSTATUS (bit 29 set) so it never collides with a real
       host fault code. */
    static constexpr uint32_t kGuestExceptionCode = 0xE0000001u;

    /* After a delay slot: set pc from branch_state/btarget/bcond, clear it, and
       return 1 (resolved) / 0 (branch_state was kNone, so a normally-entered
       block's insn[0] continues). QEMU gen_branch. __fastcall: fallthrough VA in
       ECX, jit in EDX. */
    static int __fastcall ResolveBranchHelper(uint32_t fallthrough, MipsJit* jit);

    /* Branch-likely delay-slot nullify (QEMU decode_opc "blikely not taken"): if
       branch_state==kCondLikely and not taken, set pc past the delay slot and
       return 1 (caller exits); else return 0. __fastcall: fallthrough=ECX, jit=EDX. */
    static int __fastcall NullifyLikelyHelper(uint32_t fallthrough, MipsJit* jit);

    void     Run() override;
    bool     DeepSleep()    const override { return cpu_state_.deep_sleep != 0; }
    bool     ResetPending() const override { return cpu_state_.reset_pending != 0; }
    uint32_t Pc()           const override { return cpu_state_.pc; }
    void     DispatchTraceIter() override {
#if CERF_DEV_MODE
        emu_.Get<TraceManager>().DispatchRunLoopIterMips(&cpu_state_);
#endif
    }

    std::optional<uint8_t*> PeekGuestVa(uint32_t va) override;
    uint8_t* ResolveGuestVaToHost(uint32_t va) override;

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

    /* LwrHelper: unaligned load-word-right - merge the aligned word at va&~3 into
       gpr[rt]'s low bytes (keep the high), sign-extend. __fastcall: VA=ECX,
       rt index=EDX, jit on stack. (QEMU translate.c gen_lxr + ext32s, OPC_LWR.) */
    static void __fastcall LwrHelper(uint32_t va, uint32_t rt, MipsJit* jit);

    /* LdlHelper: unaligned load-doubleword-left - merge the aligned dword at va&~7
       into gpr[rt]'s high bytes (keep the low), full 64-bit. __fastcall: VA=ECX,
       rt index=EDX, jit on stack. (QEMU translate.c gen_lxl, OPC_LDL, MO_UQ.) */
    static void __fastcall LdlHelper(uint32_t va, uint32_t rt, MipsJit* jit);

    /* LdrHelper: unaligned load-doubleword-right - merge the aligned dword at va&~7
       into gpr[rt]'s low bytes (keep the high), full 64-bit. __fastcall: VA=ECX,
       rt index=EDX, jit on stack. (QEMU translate.c gen_lxr, OPC_LDR, MO_UQ.) */
    static void __fastcall LdrHelper(uint32_t va, uint32_t rt, MipsJit* jit);

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

    /* DMULTU: {HI,LO} = gpr[rs] * gpr[rt], 128-bit unsigned (64x64 via 32-bit
       limbs, too wide for inline emit). __fastcall: rs index in ECX, rt index in
       EDX, jit on the stack. */
    static void __fastcall DmultuHelper(uint32_t rs, uint32_t rt, MipsJit* jit);

    /* DMULT (signed 128-bit product), DDIV / DDIVU (signed / unsigned 64-bit
       divide -> LO=quot, HI=rem). Too wide for inline emit. __fastcall: rs index
       in ECX, rt index in EDX, jit on the stack. */
    static void __fastcall DmultHelper(uint32_t rs, uint32_t rt, MipsJit* jit);
    static void __fastcall DdivHelper(uint32_t rs, uint32_t rt, MipsJit* jit);
    static void __fastcall DdivuHelper(uint32_t rs, uint32_t rt, MipsJit* jit);

    /* DSLLV / DSRLV / DSRAV: rd = gpr[rt] shifted by gpr[rs] & 63, full 64-bit
       (the >=32 count has no single x86-32 shift). __fastcall: rd index in ECX,
       rt index in EDX, rs index then jit on the stack. */
    static void __fastcall DsllvHelper(uint32_t rd, uint32_t rt, uint32_t rs, MipsJit* jit);
    static void __fastcall DsrlvHelper(uint32_t rd, uint32_t rt, uint32_t rs, MipsJit* jit);
    static void __fastcall DsravHelper(uint32_t rd, uint32_t rt, uint32_t rs, MipsJit* jit);

    MipsMmu*     Mmu()        { return &mmu_; }
    MipsDecoder* Decoder()    { return &decoder_; }
    PeripheralDispatcher* Peripheral() { return peripheral_; }
    MipsProcessorConfig*  CpuConfig()  { return cpu_config_; }
    MipsCp0Emitter*       Cp0Emitter() { return cp0_emitter_; }

    /* INTC pushes the live Cause.IP[5:2] LEVEL (not a latch); Run() reconciles
       cp0_cause from it on the JIT thread. Pass the full current level every
       time - a missed deassert leaves the IP bit stuck and the guest re-enters
       its ISR forever. */
    void SetExternalInterruptLevel(uint32_t ip_mask);
    void SignalIdleWake();
    void* IdleEvent() const { return idle_event_; }

    void SetResetPending(bool is_resume = false) override;
    void NotifyResetDelivered();

    void SaveCpuState(StateWriter& w)    override;
    void RestoreCpuState(StateReader& r) override;
    void SaveMmuState(StateWriter& w)    override;
    void RestoreMmuState(StateReader& r) override;
    void ResyncInterruptPoll() override;
    void FlushTranslationCache(uint32_t va, uint32_t length) override;
    void SetInjectionBand(uint32_t va, uint32_t pa, uint32_t size) override;

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
    MipsCp0Emitter*       cp0_emitter_ = nullptr;

    void*       idle_event_ = nullptr;
    std::mutex  interrupt_lock_;
    std::atomic<uint32_t> external_ip_{0};   /* Cause.IP[5:2] driven by the INTC */
    uint8_t*    interrupt_check_ = nullptr;
    uint8_t*    branch_helper_   = nullptr;
    bool        tc_flush_pending_ = false;

    /* CP0 synchronous-exception delivery, faithful to QEMU target/mips
       mips_cpu_do_interrupt set_EPC + raise_mmu_exception. */

    /* Common entry: set EPC/EXL/BD (iff !EXL), Cause.ExcCode + vector PC, clear
       branch state. refill_eligible selects the 0x000 TLB-refill offset when EXL
       was clear (else 0x180). Does NOT raise SEH (callers add it / the fetch path
       resumes directly). */
    void EnterException(uint32_t cause, bool refill_eligible);

    /* In-core Count/Compare timer, polled at the top of Run() on the JIT thread:
       advance Count by the guest cycles elapsed since the last poll and, if armed
       and Count has reached Compare, raise IP7 (the scheduler tick). */
    void TimerPoll();

    /* An interrupt is deliverable iff Status.IE && !EXL && !ERL && some
       (Cause.IP & Status.IM) bit is set (internal.h enabled/pending gates). */
    bool InterruptReady() const;

    /* Deliver a pending interrupt: EnterException(Int) to the general vector. No
       SEH (called at the top of Run(), not from inside a translated block); Run
       then dispatches the vector. */
    void DeliverInterrupt();

    /* raise_mmu_exception CP0 reg setup (BadVAddr/Context/EntryHi); shared by the
       data-fault and instruction-fetch-fault paths. */
    void SetMmuFaultRegs(uint32_t va);

    /* TLB fault on a data access: set BadVAddr/Context/EntryHi, map the TLB result
       to TLBL/TLBS/Mod, deliver, then SEH-unwind to Run(). */
    void RaiseTlbException(uint32_t va, MipsAccess acc, MipsTlbResult res);

    /* TLB miss/invalid on an instruction fetch: deliver TLBL (refill vector when
       NOMATCH) and set pc=vector, WITHOUT SEH. The JitCompile caller returns null
       and Run re-dispatches the vector (JitCompile is outside Run's __try). */
    void DeliverFetchTlbException(uint32_t va, MipsTlbResult res);

    /* Misaligned data access: set BadVAddr only, AdEL (load) / AdES (store),
       deliver, SEH-unwind. */
    void RaiseAddressError(uint32_t va, MipsAccess acc);

    /* Integer Overflow (ADD/ADDI/SUB): cause 12, no address registers, deliver,
       SEH-unwind. */
    void RaiseOverflowException();

    /* Drop the VA jump cache on an address-space switch (QEMU tlb_flush ->
       tcg_flush_jmp_cache); the per-ASID block indices persist. */
    void ContextSwitchFlush();

    /* Reset delivery (top of Run when reset_pending): run the reset-line
       listeners + armed cold-boot RAM wipe/replay, then re-establish the
       cold-power-on CPU state and re-enter at the kernel entry VA. */
    void DeliverReset();

    void* JitCompile(uint32_t guest_pc);
    void  JitDecode(uint32_t guest_pc);

    /* Decode -> emit dispatch: implemented opcode -> its place fn, else the
       loud-fatal stub. Pure (no instance state) -> static. */
    static MipsPlaceFn SelectPlaceFn(const MipsDecodedInsn* d);

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

    /* Emitted before a traced insn (when TraceManager::HasPcTrace(pc)): hands the
       live MipsCpuState to the trace layer. __cdecl(jit, pc); ESI (state) is
       callee-saved so the pinned base survives the call. */
    static void __cdecl TraceDispatchPcHelper(MipsJit* jit, uint32_t pc);

    int   LocateEntrypoints();
    void  JitCreateEntrypoints(JitBlock* containing_block, uint8_t* prefix_slab);
    size_t JitGenerateCode(uint8_t* code_location, int entrypoint_count);
    void  JitApplyFixups();
};
