# Hibernation - full machine-state save / restore

CERF can snapshot a running guest to a `.img` file and later resume it bit-for-bit
("hibernation" / "state saver"). This page is the **contract every peripheral
author must follow** so a restore comes back to a live, correct machine instead of
a half-reset one. If you create or modify ANY peripheral, SoC block, board device,
codec, or worker thread, the § "Peripheral contract" rules below are mandatory -
skipping them does not produce a smaller feature, it produces a broken restore
(dead display, frozen scheduler, missed interrupts) that surfaces hours later on a
different device.

Host-side implementation: `cerf/state/` (`hibernation.{h,cpp}`,
`state_stream.h`, `state_image_format.h`, `state_boot_gate.cpp`,
`emulation_freeze.h`, `shutdown_dialog.cpp`). The JIT pause lives in
`cerf/jit/jit_runner.{h,cpp}`.

## What it is

Three outcomes from a saved `.img`:

- **Full restore** - resume the exact desktop (every register, all RAM, all
  peripheral state) as if the machine never stopped.
- **Warm boot** - keep RAM + flash, re-init CPU / cp15 / peripherals cold; the OS
  reboots but the filesystem-in-RAM and any flash writes survive.
- **Cold boot** - ignore the image, boot from scratch.

**Why it exists:** real vintage HPC devices (Jornada 720, …) are battery-backed and
keep DRAM alive across suspend - only a battery pull wipes it - so the running OS
and its RAM-backed object store (the CE in-memory filesystem) persist across power
cycles. CERF instead wipes all guest RAM on close: every exit is effectively a cold
boot and any guest state accumulated since boot is lost. Hibernation preserves the
full running machine across an exit.

**Triggers:** Actions-menu Save/Load state; a Shutdown dialog on window close
(save-on-exit); and a boot-time prompt when a default `state.img` exists in the
device directory.

## The `.img` format

`StateImageHeader` (magic `CERFIMG1`, `format_version`, ROM fingerprint =
`rom_entry_va` + `rom_total_bytes` + `periph_layout_sig` + a `guest_additions`
byte) followed by a section count and length-framed sections
(`StateSectionHeader{ id, length }`). The length frame lets restore **skip** a
section it doesn't apply (warm boot) or tolerate a peripheral whose Save/Restore
are asymmetric without desyncing the whole stream - `SeekTo(body_start + length)`
re-aligns after every section.

Identity is validated **before** any live state is mutated (`ValidateHeader`):
wrong ROM, wrong peripheral layout signature, or a guest-additions mismatch is
refused up front. `periph_layout_sig` is a hash over the registered peripheral set
(count + each `MmioBase()`), so a cross-build-incompatible image is rejected rather
than mis-applied. Identity comes from `RomParserService`.

## Build-specific by design

CERF state images are **build-specific**: an `.img` is only
ever restored by the exact binary that wrote it. `ValidateHeader` enforces this up
front - `periph_layout_sig` (above) plus the ROM fingerprint and `format_version`
cause any image not aligned with the running build's peripheral set to be
**refused, never mis-applied**. There is deliberately no per-peripheral image
versioning; it would be intractable at CERF's chip/board count.

The consequence for the peripheral contract: the ONLY serialization requirement is
that a peripheral's own `SaveState` and `RestoreState` are **exact mirrors of each
other in the same build** (a clean round-trip). Cross-build `.img` compatibility is
**not** a requirement and must not be engineered for. A peripheral that, during
bring-up, grows its `SaveState` (new registers), reorders fields, drops a field it
no longer has, or is re-homed onto a shared core that serializes in a different
order is doing nothing wrong - the older images that used the previous format are
simply refused by `ValidateHeader`, exactly as intended.

## Sections - what each captures

Saved/restored in file order: **Cpu → Mmu → Ram → Flash → Periph → Presentation**.

- **Cpu** - the flat `ArmCpuState` POD blob (`cerf/jit/cpu_state.h`), including
  `guest_cycle_counter`, CPSR, banked regs, and `irq_interrupt_pending`.
- **Mmu** - cp15 persistent register fields only. The TLBs + SMC bitmaps are
  derived state → flushed on restore (`ArmTlbFlushAll`), never serialized.
- **Ram** - `EmulatedMemory` volatile (PAGE_READWRITE) regions.
- **Flash** - `EmulatedMemory` backed PAGE_READONLY / PAGE_EXECUTE_READ regions
  (flash writes-since-boot ARE machine state). Applied on warm boot too - flash
  survives a reboot on real hardware.
- **Periph** - every `PeripheralDispatcher::RegisteredPeripherals()` entry, in
  registration order, each tagged by `MmioBase()` and tag-checked on restore.
- **Presentation** - `HostCanvas` guest-surface dimensions, so a custom resolution
  restores its window size.

The JIT translation cache is **flushed**, never saved
(`FlushTranslationCache(0, 0xFFFFFFFF)`). After a full restore,
`ArmJit::ResyncInterruptPoll()` re-derives the interrupt-poll trampoline byte from
the restored CPU state - without it a restored-pending IRQ is silently missed.

## The two-thread freeze model - read before touching ANY peripheral

A safe snapshot requires the guest to be quiescent. Two things execute guest-visible
state, and pausing one does NOT pause the other:

1. **The JIT (guest CPU) thread.** `JitRunner::Pause()` parks it between translated
   blocks; `Resume()` releases it. `Pause()` is **host-thread only** - calling it
   from the JIT thread self-deadlocks. Hibernation does `Pause() → work → Resume()`.

2. **Peripheral worker threads.** OST match loops, ADC/battery samplers, PMIC,
   keypad, network, serial - these are `std::thread`s that keep mutating
   guest-visible state regardless of the JIT pause. They are frozen by
   **`EmulationFreeze`** (`cerf/state/emulation_freeze.h`):
   - A worker holds `WorkerSection()` (a `shared_lock`) **around the part of each
     iteration that reads or writes guest state**.
   - Hibernation holds `SnapshotSection()` (a `unique_lock`) across the whole
     save or restore.
   - **Lock-order invariant (deadlocks if violated):** the freeze lock is taken
     BEFORE any peripheral mutex, and a worker **never** holds `WorkerSection`
     across a cv wait / sleep / thread join. Acquire it, do the state touch,
     release it, then wait.

Reference worker: `OsTimer::MatchLoop` in `cerf/socs/os_timer.h` -
`{ auto frozen = freeze.WorkerSection(); RebaseToCurrent(); CheckAndFire(); }`
then re-locks the cv mutex and waits OUTSIDE the worker section.

## The peripheral contract - MANDATORY when you create or modify a peripheral

A `Peripheral` (or any object holding mutable guest-visible state) gets up to three
methods. **Every peripheral with mutable state MUST implement the first two.**

### 1. `SaveState(StateWriter&)` / `RestoreState(StateReader&)`

Serialize **every mutable register / latch / counter / FIFO**, not just an obvious
`storage_[]` array - timer counters, DMA transfer registers, RTC base, LCD
framebuffer config, blit-engine latched-op params, FIFO contents, mode/command FSM
latches. Save and Restore must be **exact mirrors** (same field order). Use
`#include "../../state/state_stream.h"`; `w.Write<T>()` / `r.Read<T>()`;
length-prefix variable-size data (member-vector NAND/NOR, FIFOs) so forward-skip
stays valid. `std::atomic<uintN>` → `.load(std::memory_order_acquire)` /
`.store(v, std::memory_order_release)`.

### 2. `PostRestore()`

Runs in a **second pass after every peripheral's `RestoreState` has completed**
(`Hibernation::RestorePeripherals`), so it is order-independent - all registers are
already in place. Use it to re-assert **computed** state a single `RestoreState`
cannot establish, above all the interrupt line `source → INTC → JIT`:

- An **INTC** re-notifies the JIT of its restored pending/mask state
  (`SetInterruptPending` / re-derive `HasPendingUnmasked`). A restored INTC that
  only reloads its registers never re-arms the JIT → missed or stale IRQ → hang.
- A **level-driving source** (GPIO edge/level lines, OST match level, an SA-1111
  cascade) re-drives its INTC source level. See `sa11xx_intc` (`NotifyLocked`),
  `os_timer` (`PushMatchLevelLocked`, moved OUT of `RestoreState` into
  `PostRestore`), `sa11xx_gpio` (`PublishEdgeSourcesLocked`), `sa1111_intc`
  (`DriveCascadeOutput`).

`PostRestore` is a no-op default in `peripheral_base.h`; override it only when you
own a computed line. **Fix the whole bug class, not one instance** - if one INTC
needs `PostRestore`, audit every INTC.

### 3. Worker-thread wrapping

If your peripheral spawns a worker thread that touches guest state, wrap the
state-touch in `emu_.Get<EmulationFreeze>().WorkerSection()` per the freeze model
above.

## Patterns by peripheral shape

- **Unified peripheral** (`class Foo : public Peripheral` holding its own regs) -
  SaveState/RestoreState directly on the class.
- **Split peripheral** (a stateless MMIO `Peripheral` delegating to a state-owner
  registered `_AS` a base, e.g. S3C2410 INTC) - the **state-owner** implements
  SaveState/RestoreState/PostRestore; the MMIO override delegates via
  `static_cast<Owner&>(emu_.Get<Base>())`. **Check BOTH `.h` and `.cpp`** before
  calling a peripheral a gap - Save/Restore is often inline in the header.
- **Codec / PMIC parts are Services, NOT Peripherals** → they are not in the
  `RegisteredPeripherals()` walk. Add virtual `SaveState/RestoreState` to the codec
  base (`Ac97Codec`, `Sa11xxMcpCodec`), override in the concrete (serialize its
  `reg_`), and **forward from the owning peripheral's SaveState/RestoreState** via
  `if (auto* c = emu_.TryGet<Base>()) c->SaveState(w);` (symmetric in Save+Restore).
- **Non-`Peripheral` stateful objects** (PCMCIA `PcmciaSlot` / `PcmciaCard`,
  sub-devices like a companion-ASIC `Ps2Mouse`) are not auto-enumerated → they need
  an explicit serialization walk + card-presence recreation
  (`PcmciaCardCatalog::Create(id, binding)`).
- **Rebase timers** (guest-cycle: OST/synctimer/gptimer/epit/gpt; wall-clock:
  `odo_cpu_timer`) - **never raw-serialize a `std::chrono::time_point` or a
  guest-cycle baseline.** Save the live counter; on restore re-anchor the baseline
  so the counter resumes continuously: guest-cycle → `baseline = (saved_count,
  GuestCycles())`; wall-clock → `period_start_ = Clock::now()`. Per-channel match
  anchors stay valid (same counter domain).
- **In-flight host coupling** resets on restore (no host sink / pen / socket exists
  post-restore): clear audio-DMA `in_flight`/`tx_running`, touch `pen_down`/
  `pen_timer_enabled`, etc. in RestoreState or PostRestore. See `sa11xx_dma`,
  `sa1111_sac`, `odo_touch_sound`.

## What NOT to serialize (host-side members)

Skip anything that is host machinery, not guest state - it is reconstructed, not
restored: raw host pointers, `std::thread`, `HANDLE`, audio sinks, `std::function`,
pointers into the object's own buffer, `std::string`/`std::vector` TX-line
accumulators. A **file-backed `DiskImage`** (host `HANDLE`) is NOT serialized - it
persists on host disk across the restart; only its in-flight transfer state
(`AtaDrive`) is.

## Verifying a peripheral's serialization

A peripheral compiling is not a peripheral serialized. The completeness check is to
read every `public Peripheral` (both `.h` and `.cpp`) and confirm it has
SaveState/RestoreState, then do a **save → restore round-trip on the actual
device** and exercise it. A single clean round-trip on one OS does not prove
restore correct - per-device runtime testing does.

## Common failure shapes

- A peripheral resets on restore (its registers reload but a `PostRestore`-computed
  line / cleared in-flight flag is missing) → dead display or frozen scheduler.
- A worker thread mutates state mid-snapshot (missing `WorkerSection`) → torn,
  inconsistent image.
- A rebase timer raw-saved its baseline → the clock jumps or stalls on restore.
- An INTC reloaded registers but never re-notified the JIT (missing `PostRestore`)
  → a pending IRQ is lost and the guest hangs.
