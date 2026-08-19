# JIT - guest ISA → host x86 block translator

The JIT translates guest code to native host x86 at the first
execution of each block, and caches the translation. CERF implements
two guest ISAs: ARM (ARM-mode + Thumb) and MIPS (MIPS IV, 64-bit). A
board runs exactly one of them, chosen by `BoardContext::GetCpuArch()`.

## Where the parts live

| Path | What is there |
|---|---|
| `cerf/jit/guest_engine.h` | the ISA-neutral engine interface `JitRunner` drives |
| `cerf/jit/jit_runner.{h,cpp}` | the host thread that owns the dispatch loop |
| `cerf/jit/jit_code_arena.{h,cpp}` | the executable-memory allocator |
| `cerf/jit/isa_block_space.h`, `jit_block_index.{h,cpp}` | the translation index and jump cache |
| `cerf/jit/x86_emit*.h` | host x86 encoding helpers, from Intel SDM Vol. 2 |
| `cerf/jit/arm/`, `cerf/jit/mips/` | the two engines |
| `cerf/jit/arm/place/`, `cerf/jit/mips/place/` | one file per emitted guest instruction |
| `cerf/cpu/<core>/` | per-core strategies the engines resolve |

## The `GuestEngine` seam

`JitRunner` drives an abstract `GuestEngine` service and never names a
concrete engine. `ArmJit` and `MipsJit` each register as that base, and
the `CpuArch` of the board selects which one wins the slot.

The seam is ISA-neutral throughout. Hibernation, reset, deep sleep and
guest-additions injection all reach the engine through it. The state
and power layers therefore carry no ISA knowledge.

## Boundary

Host C++ in the JIT covers this, and nothing else:

- translation, from decode to emit
- the dispatch loop
- the MMU and coprocessor (cp15 / CP0) state
- the per-SoC configuration strategies
- exception entry
- the cross-thread interrupt channel
- the SEH fault wrapper

It does not cover CE userspace or kernel behavior. Those are guest
code that runs in the cache. If `coredll.dll` does something wrong,
the fix is not in the JIT.

## How a block is translated

A block is a straight-line run of guest instructions. The engine
translates the first instruction and takes its physical address as the
block's identity. It then decodes forward until one of these ends the
span:

- the end of the page
- a write to the PC
- a context synchronization operation
- a decode reject
- a failed translate

The engine then allocates one arena slab and places the block record
at the slab head. It emits host code for each decoded instruction in
turn, then returns the unused tail to the arena.

A block that outgrows its slab evicts itself. It flushes the arena and
the block index, then compiles once more.

A block returns to the dispatcher at its end. The dispatcher resolves
the next block through the jump cache. The guest PC must be live in
CPU state before that exit, because the dispatcher reads the next PC
from there.

The ARM engine chains a direct branch back to the start of the block
that contains it. That block jumps to its own entry and does not
return.

A chained block never reaches the dispatcher, so it observes nothing
that the dispatcher delivers. Emitted code polls one word in CPU state
before each chained jump. When that word is not zero, the block
returns to the dispatcher. Every signal that must stop a chained block
therefore owns a bit in that word.

## The `place_fn` contract

Both engines share this convention. The decoder fills a
decoded-instruction record and assigns a function pointer. The emit
phase then runs that function once per guest instruction. Each one
writes host machine code at a cursor and returns the advanced cursor.

Place fns live one per file. To add a guest instruction, add a decoder
mapping and add a `place/<name>.cpp` - no build-script or project-file
edit is necessary, because `cerf.vcxproj` globs `**\*.cpp`.

A place fn reaches per-instance state only through its block context.

**The decode contract**: a decoder that returns success supplies a
place fn that is either implemented or born-fatal. A decoder that
returns failure means the encoding is architecturally UNDEFINED or
UNPREDICTABLE, and the caller raises a guest Undefined-Instruction
exception.

## Pinned-register dispatcher

Every translated block runs with the guest CPU-state pointer pinned in
`ESI`. ARM additionally pins the MMU-state pointer in `EBX`. Place fns
address state fields off the pinned register and never recompute the
base.

**The assignment is invariant across every place fn and every JIT
helper.** Changing it requires an edit at every emit site.

## Invariants that span files

None of these is visible from any single file.

- **A service that resolves an engine-specific service from `OnReady`
  must gate `ShouldRegister` on `CpuArch` itself.** On a board of the
  other ISA that slot has no winner. The service then fails to resolve,
  and the board dies at boot. Nothing infers the gate, and neither the
  compiler nor the linker sees the omission.
- **A block's physical identity comes from the fetch, never a
  re-walk.** An independent later walk diverges from the fetch during
  transitional MMU states. Two such states are a TLB-cached mapping
  the fresh walk cannot see, and a partially-built page table. The
  walk then mis-keys the block, or faults it spuriously.
- **Short jumps silently truncate.** `Jcc rel8` / `JMP rel8` carry a
  signed-byte displacement. When the emit between a jump and its
  target grows past that range, the back-patch truncates and the CPU
  jumps into garbage. The fixup helper fatals at emit time instead.
  The fix is the `rel32` form of that jump.
- **A flush never reclaims a permanent arena allocation.** The
  trampolines are permanent, because emitted blocks reach them through
  a baked absolute address. If a flush reclaims those bytes, every
  such block jumps into unrelated code.
- **The JIT reaches a cp15 trampoline with `CALL`, so control RETURNS
  into the calling block.** A handler that frees the arena under that
  block returns into freed memory. An SCTLR write therefore pends its
  cache flush instead of performing it.
- **FCSE fold is identity on ASID kernels.** The cp13 fold separates
  per-process low VAs only on FCSE kernels (CE5 / ARMv5). CE6 / CE7
  leave the fold base at zero and distinguish address spaces by ASID
  instead. A VA-keyed structure that must stay process-private
  therefore has to incorporate the ASID, and must not rely on the fold.
- **The interrupt line is a LEVEL, not an edge.** Peripheral threads
  publish the current level. The engine folds that level on the JIT
  thread at the top of each iteration. A publisher that sends less
  than the full current level leaves a stuck bit, and the guest
  re-enters its ISR forever. Two words carry that level across
  threads: the line, and the chain-exit bit that returns a chained
  block to the dispatcher. One channel owns the line and that bit.
- **A reset re-enters at a PHYSICAL address.** The reset must
  therefore turn the MMU off and clear the FCSE fold base. Otherwise
  the fetch folds that address and walks it through the page tables of
  the process that ran before the reset. Deep-sleep resume overrides
  those reset values - see [agent_docs/deep_sleep.md](deep_sleep.md).

## I/O routing

An ARM translate returns null for two different reasons: a real fault,
or a resolved PA that lies in peripheral space. The MMU records the
peripheral PA, and a separate flag shows whether that record is live.
The flag alone separates the two cases. A peripheral can occupy
physical address zero.

**Every path that reaches the abort tail must leave that record in a
defined state.** The walker clears it on entry, and a path that never
runs the walker clears it itself. A translation-free path that does
not clear the record sends its abort into the interpreter.

A peripheral access happens exactly once. Emitted code either performs
it and completes the instruction, or it performs no access and leaves
the block. The interpreter then re-executes the full instruction.

The record that carries the peripheral address holds one address. An
access with more than one address therefore cannot complete inside the
block.

A peripheral access has a third outcome. The interrupt channel owns
one gate over the interrupt line, the CPSR interrupt mask, and the
sleep state. The dispatcher raises on that same gate, so one predicate
serves both. When the gate passes, no access happens, and the channel
sets the guest PC back to this instruction. Control then leaves the
block.

The guard takes two forms. Emitted code carries it at the peripheral
branch of a translation, and clears the record itself. A load-store
helper carries it at its own peripheral branch, and clears the record
there. The helper form also applies only before the access moves any
bytes.

The dispatcher then re-reads the line. When the line is still high,
the dispatcher raises the interrupt, and the instruction runs after
the handler. A peripheral thread can clear the line first. The
instruction then runs again with no interrupt. The back-out does not
change the interrupt line.

This path runs only when two conditions are true together. The
translation resolved the access to peripheral space. The instruction
commits no state before this point. Every other access keeps delivery
at block boundaries.

---

# MIPS engine

`MipsJit` is structured in parallel to `ArmJit`: same arena, same block
index, same place-fn contract. Each engine indexes each instruction set
in its own block space. The second set is Thumb on ARM and MIPS16 on
MIPS.

The parts that differ from ARM:

- **CP0 exception model.** Synchronous exceptions follow the model of
  QEMU `target/mips` `mips_cpu_do_interrupt`. The common entry sets
  EPC, the level bits and the cause code. It picks the refill vector
  when the level bit was clear, and the general vector otherwise. A
  guest exception raised inside a helper unwinds to the dispatch loop
  through a customer-defined NTSTATUS, and resumes at the already-set
  vector PC.
- **kseg fold.** `kseg0` and `kseg1` map directly, cached and uncached
  respectively. Mapped segments walk the software TLB. The EntryHi
  ASID distinguishes address spaces, and an ASID change flushes the VA
  jump cache.
- **Wide operations go through helpers**, because they cannot emit
  inline on x86-32. These are the doubleword loads and stores, the
  unaligned merges, the 128-bit products, doubleword division, and the
  variable doubleword shifts.
- **Unimplemented paths loud-fatal, never silent-UND.** A decoder
  reject or a not-yet-built place fn logs the opcode and PC, then
  exits.
