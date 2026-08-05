# JIT - guest ISA → host x86 block translator

The JIT translates guest code to native host x86 at the first
execution of each block, and caches the translation. CERF implements
two guest ISAs: ARM (ARM-mode + Thumb) and MIPS (MIPS IV, 64-bit). A
board runs exactly one of them. The board's CPU architecture selects
which one.

## The `GuestEngine` seam

`JitRunner` drives an abstract `GuestEngine` service
(`cerf/jit/guest_engine.h`) and never names a concrete engine.
`ArmJit` and `MipsJit` each `REGISTER_SERVICE_AS(<engine>,
GuestEngine)`. `BoardContext::GetCpuArch()` (`enum class CpuArch {
Arm, Mips }`) selects the winner. `ArmJit::ShouldRegister` therefore
returns true on `CpuArch::Arm`, and `MipsJit::ShouldRegister` returns
true on `CpuArch::Mips`. Every ARM-engine service (`ArmCpu`, `ArmMmu`,
`ArmDecoder`, `ArmCp15SctlrHandler`) likewise gates `ShouldRegister`
on `CpuArch::Arm`. The MIPS engine's internals gate on
`CpuArch::Mips`. A board therefore materializes one engine and none of
the other ISA's services.

The seam surface (all ISA-neutral):

- `Run` / `Pc` / `DeepSleep` / `ResetPending` - the dispatch loop and
  CPU-status reads `JitRunner` polls.
- `SaveCpuState` / `RestoreCpuState` / `SaveMmuState` /
  `RestoreMmuState` - the hibernation `Cpu` and `Mmu` `.img` sections
  route here, so the state layer is ISA-agnostic.
- `SetResetPending(is_resume)` - `GuestCpuReset` / `GuestColdBoot`
  pend a CPU reset here. `is_resume` selects the deep-sleep-wake
  notification over the reboot one.
- `FlushTranslationCache`, `SetInjectionBand`
  (guest-additions overlay band), `PeekGuestVa` /
  `ResolveGuestVaToHost` (diagnostics + ROM placement).

## Boundary

- **Host C++ in the JIT covers**: translation (decode → emit), the
  dispatch loop, the MMU + coprocessor (cp15 / CP0) state, per-SoC
  configuration strategies, exception entry, the cross-thread
  interrupt channel, and the SEH fault wrapper. That is all.
- **NOT covered by host C++**: any CE userspace or kernel behavior.
  Those are guest code that runs in the cache. If `coredll.dll` does
  something wrong, the fix is not in the JIT.

## Shared infrastructure (both engines)

- **`JitCodeArena`** (`cerf/jit/jit_code_arena.{h,cpp}`) - one large
  `VirtualAlloc PAGE_EXECUTE_READWRITE` region per engine instance,
  bump-allocated. An allocation failure means the region is full. The
  caller then flushes everything and retries. `Flush` drops every
  block but keeps the region committed. The unused tail consumes no
  physical RAM under Windows overcommit.
- **`IsaBlockSpace`** (`cerf/jit/isa_block_space.h`) - the per-ISA
  translation index, modeled on QEMU's TCG block cache. It holds a
  VA-indexed jump cache (`tb_jmp_cache`), a `global` `JitBlockIndex`
  for nG=0 kernel/shared blocks, and 256 per-ASID `JitBlockIndex`
  trees for nG=1 user blocks. It also holds a per-physical-page
  intrusive list of outer blocks (`PageDesc.first_tb`), so SMC
  `RemoveRange` walks one page's list instead of the whole VA tree.
  The block index is phys-keyed. The jump cache uses the FCSE-folded
  VA as its key, and a context switch, an SMC, or a full flush drops
  it. The ARM engine owns two spaces (ARM, Thumb). The MIPS engine
  owns one.
- **`JitBlockIndex`** (`cerf/jit/jit_block_index.{h,cpp}`) - one
  ordered map per address space, keyed on the post-fold guest VA.
  Outer entries cover `[guest_start, guest_end]` ranges. Lookups are
  find-exact and range-intersect, and they tolerate overlapping
  ranges: a re-entry at a non-start instruction compiles as its own
  outer block. Each record lives at the head of its arena slab
  (`PlaceOuterAt`). Eviction removes the exact record the lookup
  returned (`IsaBlockSpace::RemoveBlock`).
- **`x86_emit.h`** - header-only x86 encoding helpers. Style: free
  functions that take `uint8_t*& cursor` and advance the cursor past
  the emitted bytes. The encoding source of truth is Intel SDM Vol. 2.

  **`rel8` vs `rel32` trap.** Short conditional jumps (`Jcc rel8` /
  `JMP rel8`) carry a signed-byte displacement (±127). If the emit
  between the jump and its target grows past that range - usually
  because the surrounding instruction's code body grew - the
  back-patch silently truncates and the CPU jumps to garbage. The
  `FixupLabel` helper catches the overflow at emit time with a fatal
  log that names the jump opcode and the actual displacement. The fix
  is to switch the affected jump to its `*32` cousin (`EmitJzLabel32`,
  `EmitJnzLabel32`, `EmitJmpLabel32`, `FixupLabel32`).

**Block physical identity comes from the fetch, never a re-walk.**
If a block is keyed or validated by physical address, that PA must
come from the same translation that fetched the block's bytes. An
independent later page-table walk diverges from the fetch during
transitional MMU states (a TLB-cached mapping the fresh walk cannot
see, or a partially-built page table). Such a walk mis-keys the block
or faults it spuriously.

## The `place_fn` contract

This is the single most important convention in the JIT, and both
engines share its shape. The decoder fills a decoded-instruction
record and assigns a function pointer. The emit phase then runs that
function once per guest instruction. The ARM form:

```cpp
using ArmPlaceFn = uint8_t* (*)(uint8_t*       cursor,
                                DecodedInsn*   d,
                                BlockContext*  ctx);
```

`MipsPlaceFn` has the same shape over `MipsDecodedInsn` /
`MipsBlockContext`. Each `place_fn` writes host machine code at
`cursor` and returns the advanced cursor. ARM place fns live in
`cerf/jit/arm/place/`, MIPS place fns in `cerf/jit/mips/place/` -
one file per function. To add a guest instruction, add a decoder
mapping and add a `place/<name>.cpp`. No build-script or project-file
edit is necessary (`cerf.vcxproj` globs `**\*.cpp`).

The block context carries per-block emit state. That state is the
decoded-instruction array, the owning engine back-pointer for
service access, the addresses of the per-instance JIT trampolines,
and per-block caches. Place fns reach per-instance services through
`ctx->jit`.

## Pinned-register dispatcher

Every translated block runs with the guest CPU-state pointer pinned
in `ESI` across the whole block. ARM additionally pins the MMU-state
pointer in `EBX`. Place fns address state fields as `[<pinned-reg> +
byte-offset]` and never recompute the base. The assignment is
invariant across every place fn and every JIT helper - a change to it
requires an edit at every emit site. Helpers documented as
`__fastcall(va, …, jit)` obey that convention, because emit code at
the call site already has the args in the right registers.

---

# ARM engine (`ArmJit`)

## Service set

The ARM engine is a small constellation of services. Each owns one
responsibility:

- **`ArmJit`** - the translation cache, the dispatcher, and the
  compile pipeline. It implements `GuestEngine`. Hot-path runtime
  helpers called from emitted code live here as static methods, so
  emit code can bake a stable function-pointer address.
- **`ArmCpu`** - owns `ArmCpuState` (GPRs, CPSR, banked SPSR/R13/R14
  per privileged mode). Hosts the mode-bank / CPSR-write helpers and
  the exception-entry methods (Undefined, AbortData, AbortPrefetch,
  IRQ, SWI, Reset).
- **`ArmMmu`** - owns the cp15 register file, the I-TLB and D-TLB,
  the page-table walker. `Translate{Read,Write,ReadWrite,Execute}`
  are the public surface. On a peripheral PA they return `nullptr`
  and stash the PA in an I/O-pending slot. The JIT helper reads that
  slot and routes the access to `PeripheralDispatcher`.
- **`ArmDecoder`** - ARM/Thumb opcode → `DecodedInsn`. Thumb decoders
  synthesize an ARM equivalent and re-issue through the ARM decoder
  so register/operand placement has one canonical source.
- **`ArmProcessorConfig`** - per-SoC strategy. PC store offset,
  base-restored-abort model, cache-line size, MIDR/CTR,
  DSP / LDRD-STRD optionality. Base in
  `cerf/cpu/arm_processor_config.h`, concretes under
  `cerf/cpu/<core>/`, selected by `GetSoc()`.
- **`CoprocEmitter`** - per-SoC MCR/MRC/CDP/LDC/STC emit strategy.
  Base in `cerf/jit/arm/coproc_emitter.h`, concretes under
  `cerf/cpu/<core>/`.
- **`ArmCp15SctlrHandler`** - owns the per-instance trampoline that
  cp15 c1 (SCTLR) writes JMP to. SCTLR changes flip the MMU on and
  off, and thus invalidate every cached block - the handler flushes
  the cache before it returns, so JIT-emitted code never re-enters a
  freed block.

A shared-capable ISA capability (VFP, NEON, DSP, …) lives in the
shared ARM decode/emit path behind an `ArmProcessorConfig::HasX()`
flag, never localized in one SoC's `CoprocEmitter`.

## Compile pipeline

`ArmJit::JitCompile(guest_pc)` runs a multi-phase pipeline:

1. **Decode forward** from `guest_pc` until a kernel boundary, a
   prefetch-abort PA, or a per-block instruction cap. CPSR selects
   ARM or Thumb.
2. **Locate entrypoints + flag-eliminate**. Every R15-modifying
   insn terminates an entrypoint. In-stream branches create new
   entrypoints at their destinations. A back-to-front sweep drops
   per-insn flag packs the next consumer overwrites.
3. **Allocate + register entrypoints**. Bump-allocate one slab from
   `JitCodeArena`, place the records, insert into the ISA's
   `IsaBlockSpace`. Outer entrypoints become new ranges. A re-entry
   into an existing range compiles as its own outer block.
4. **Generate code**. Walk the decoded stream, emit per-entrypoint
   prologue + per-condition guard + per-instruction `place_fn`.
5. **Apply fixups**. Back-patch intra-batch forward branches whose
   destination native address was not known at the time of the JMP
   emit.
6. **Flush host instruction cache** for the emitted range.

On translation-cache exhaustion: full flush + retry with a fresh
slab.

## FCSE fold

Guest VAs less than 32 MB are private to the current process (ARM
Fast-Context-Switch Extension - the OS plants the active process's
fold base in cp13). The block-index key, the TLB key, and the
shadow-stack key all use the post-fold VA. `DecodedInsn::guest_address`
keeps the raw VA for diagnostics and for instruction-stream
re-entry.

**FCSE fold is identity on ASID kernels.** cp13 FCSE `process_id`
separates per-process low VAs only on FCSE kernels (CE5 / ARMv5).
CE6 / CE7 set `process_id = 0` and distinguish address spaces by ASID
(CONTEXTIDR) instead, so the fold is a no-op there. Any VA-keyed
structure that must stay process-private must therefore incorporate
the ASID, and must not rely on the fold.

## Trampoline pattern

For cross-block control transfers (cp15 cache-op-induced flush,
R15-modified resolve, branch resolve, BL push, BX-LR pop,
data-abort raise, …) the JIT emits a JMP/CALL to a per-instance
trampoline, and does not inline the resolve. Each trampoline is a
small naked-machine-code body in writable host memory, and `ArmJit`
owns it.

## Shadow stack

`BL` (and `BL`-shaped patterns the decoder recognizes) push a
`(guest_return_addr, cached_native_dest)` pair onto a per-instance
shadow stack. `BX LR` / `MOV PC, LR` pop and compare. On a
guest-return-address match the JIT JMPs straight to the cached
native destination, and skips the R15-modified-helper round trip.
Every JIT cache flush clears the shadow stack - the cached pointers
became stale the moment the arena was reused.

## I/O routing

`ArmMmu::Translate*` returns `nullptr` for two reasons: (1) a real
fault (TLB miss + page-table fault), and (2) the resolved PA lies
in peripheral I/O space. The JIT helper distinguishes the two when it
reads the MMU's I/O-pending slot - non-zero means "PA is here, route
to `PeripheralDispatcher`," zero means "raise the abort." The slot is
per-`ArmMmu` instance.

## Interrupt delivery (cross-thread)

Peripheral threads (the board / SoC INTC concretes) drive the external
IRQ line level through `ArmJit::SetInterruptPending` /
`ClearInterruptPending`. The engine keeps that level in one
`std::atomic` word - the only interrupt state that crosses threads -
and a 0→1 edge signals the idle event, so a JIT thread parked in a
WFI wait wakes. On the JIT thread, `Run` reads the live level at the
top of every iteration, folds it into
`ArmCpuState::irq_interrupt_pending`, and delivers through
`ArmCpu::RaiseIrqException` when CPSR.I is clear and the CPU is
neither halted nor resetting; the vectored PC then dispatches like
any other block. The MIPS engine has the same shape
(§ In-core timer + interrupts). After a hibernation restore the
INTC's `PostRestore` re-drives the line, and `Run` picks it up like
any other iteration.

## Reset

- **Cold power-on**: the boot path resolves the entry VA, writes
  the bootloader-handoff SP via `ArmCpu::SetInitialStackPointer`,
  then calls `RaiseResetException(initial_pc)`. The initial PC is
  cached for subsequent soft resets.
- **Soft reset** (watchdog expiry, OAL request, …): the peripheral
  calls `ArmJit::SetResetPending`. On the next IRQ-delivery
  dispatch, the JIT routes through `ArmCpu::RaiseResetException()`
  (no-arg overload, uses the cached entry VA).

## SEH fault filter

`ArmJit::Run` wraps `Dispatch` in `__try`/`__except`. The filter
dumps the host EIP context (host x86 registers + a window of
JIT-emitted bytes around the fault), the guest ARM register file,
and the host symbol resolved via `dbghelp`, then `CerfFatalExit`s.
The filter only runs on actual hardware exceptions - zero hot-path
cost on the dispatch path.

---

# MIPS engine (`MipsJit`)

`MipsJit` (`cerf/jit/mips/`) is the MIPS IV / 64-bit engine,
structured in parallel to `ArmJit`. It owns a `JitCodeArena`, a
single `IsaBlockSpace` (one ISA - no ARM/Thumb split), a
`MipsDecoder`, a `MipsMmu`, and a 64-bit `MipsCpuState`. It also
resolves a `MipsProcessorConfig` + `MipsCp0Emitter`. `ESI` is pinned
to `MipsCpuState*`. Every emitted block addresses GPR / CP0 / TLB
fields off `ESI`.

## Service set

- **`MipsJit`** - translation cache, dispatcher, and compile
  pipeline. It implements `GuestEngine`. Memory access, CP0 side
  effects, TLB ops, exception delivery, and the wide 64-bit
  arithmetic that cannot emit inline are `static __fastcall` helpers.
  `MipsJit` bakes them into emitted code.
- **`MipsMmu`** (`cerf/jit/mips/mips_mmu.{h,cpp}`) - the kseg fold +
  software joint-TLB. `kseg0/kseg1` map directly (unmapped/cached
  vs uncached). Mapped segments walk the software TLB. `TLBWI` /
  `TLBWR` / `TLBP` / `TLBR` drive the indexed / random / probe /
  read TLB ops (and their block-cache invalidation).
- **`MipsDecoder`** - MIPS opcode → `MipsDecodedInsn`.
- **`MipsProcessorConfig`** (`cerf/cpu/mips_processor_config.h`) -
  per-SoC strategy, the MIPS analog of `ArmProcessorConfig`: `Prid`,
  `TlbSize`, `IsaLevel`, and the silicon-capability gates `HasFpu`
  (CP1), `HasLlsc` (LL/SC), `HasCounter` (CP0 Count/Compare),
  `HasWatch` (CP0 WatchLo/Hi) - the cpuinfo_mips option set. Base in
  `cerf/cpu/`, concretes under `cerf/cpu/<core>/`, selected by
  `GetSoc()`.
- **`MipsCp0Emitter`** (`cerf/jit/mips/mips_cp0_emitter.h`) - the
  per-SoC MFC0 / MTC0 / DMFC0 / DMTC0 emit strategy (the CP0 moves -
  TLB ops and timer side-effects are `MipsJit` helpers), the MIPS
  analog of `CoprocEmitter`. Base in `cerf/jit/mips/`, concretes under
  `cerf/cpu/<core>/`.

## CP0 exception model

Synchronous CP0 exceptions use the model of QEMU target/mips
`mips_cpu_do_interrupt`. The common entry sets EPC / EXL / BD (iff
!EXL), `Cause.ExcCode`, and the vector PC - the `0x000` TLB-refill
offset when EXL was clear, else `0x180` general. `ERET` returns to
`ErrorEPC` (if `Status.ERL`) else `EPC`, clears the level bit and
LLbit. Covered causes: TLB load/store/modify (`TLBL` / `TLBS` /
`Mod`), address error (`AdEL` / `AdES`), integer overflow,
`SYSCALL` / `BREAK`. A guest exception raised from inside a
memory/arith helper unwinds to `Run`'s `__except` via a
customer-defined NTSTATUS (`kGuestExceptionCode`). That handler
resumes at the already-set vector PC.

**Unimplemented MIPS paths loud-fatal, never silent-UND.** A decoder
reject or a not-yet-built place fn emits `PlaceMipsUndefined`, which
logs op + PC and `CerfFatalExit`s. Trapping arithmetic overflow and
unbuilt MMIO/CP0 paths fatal the same way.

## In-core timer + interrupts

`Run` polls the CP0 Count/Compare timer at its top, on the JIT
thread. Count advances by the guest cycles elapsed since the last
poll. If the timer is armed and Count reaches Compare, the timer
raises IP7 (the scheduler tick) - QEMU `cp0_timer.c`. The board INTC
pushes the live `Cause.IP[5:2]` external-interrupt LEVEL (not a
latch) via `SetExternalInterruptLevel`. `Run` reconciles `cp0_cause`
from it on the JIT thread. You must pass the full current level every
time - a missed deassert leaves an IP bit stuck, and the guest
re-enters its ISR forever. An interrupt is deliverable iff
`Status.IE && !EXL && !ERL` and some `(Cause.IP & Status.IM)` bit is
set.

## Address spaces + wide ops

MIPS distinguishes address spaces by EntryHi ASID. An ASID-field
change flushes the VA jump cache (`Mtc0EntryHiHelper` →
`tlb_flush`). The 64-bit operations too wide for inline x86-32 emit
go through helpers. These are doubleword loads and stores, the
unaligned `LWL/LWR/LDL/LDR` + `SWL/SWR/SDL/SDR` merges,
`DMULT/DMULTU` 128-bit products, `DDIV/DDIVU`, and the variable
doubleword shifts `DSLLV/DSRLV/DSRAV`. Memory access uses per-width
all-in-one helpers (`__fastcall` VA in ECX). Each helper folds kseg or
walks the software TLB, then performs the access or routes a non-RAM
PA to `PeripheralDispatcher`.
