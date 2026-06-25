# Deep Sleep - guest suspend / resume

CERF models guest deep sleep / suspend: when the running guest powers its CPU
down (a SoC power-down register write, or a CPU power-mode instruction), CERF
halts the virtual CPU, shows a no-timeout "Shut down CERF?" recovery dialog, and
on Cancel wakes the guest so it resumes exactly where it slept - zero data loss.
This is the GUEST's own suspend/resume running on faithful virtual hardware; CERF
only halts the CPU and drives the wake.

**This is not hibernation.** See § "Suspend/resume is not hibernation" - conflating
the two is the most common mistake in this subsystem.

Host implementation: `cerf/host/guest_deep_sleep.{h,cpp}`,
`cerf/state/shutdown_dialog.{h,cpp}`. CPU halt/wake: `cerf/jit/arm/arm_jit.cpp`
(`EnterDeepSleep`, `SetResetPending`), `cerf/jit/jit_runner.cpp` (the park),
`cerf/jit/arm/arm_cpu_exceptions.cpp` (delivery), `cerf/jit/arm/arm_cpu.cpp`
(`DoRaiseReset`). Reset cause + reset line: `cerf/socs/guest_cpu_reset.{h,cpp}`.

## The halt model

`ArmCpuState::deep_sleep` is the halt flag. `ArmJit::EnterDeepSleep()` sets it and
re-arms the interrupt poll; the poll's delivery helper (`ArmCpuRaiseIrqException`)
returns `nullptr` while `deep_sleep` is set, which unwinds to `ArmJit::Run()`
returning, and `JitRunner::RunLoop` then parks the JIT thread - the same park a
reset uses. A delivered reset clears `deep_sleep` and releases the park. No guest
instructions run while parked, so no peripheral IRQ reaches the sleeping CPU.

`GuestDeepSleep::Enter()` (JIT thread) banners the sleep, halts the CPU, and posts
`Recover()` to the UI thread. `Recover()` shows `ShutdownDialog::Show(...)`: Cancel
wakes (`DeliverWake`), OK exits (optionally saving). The recovery dialog is
**no-timeout** by design - a countdown could auto-exit and discard live guest RAM
while the user is away.

## The wake IS a reset

A sleep wake is delivered through the reset path, not as an in-place resume:
`GuestDeepSleep::DeliverWake()` latches the wake cause and calls
`ArmJit::SetResetPending()`. The guest's OWN kernel/bootloader resume code - which
runs at the reset/boot entry - is what restores the live session. Modelling the
wake as "no reset / resume in place" is wrong: real SoCs apply an internal reset
on sleep-exit, and the boot path reads the reset-cause register to take its resume
branch.

Because the wake re-enters through reset, two things must hold:

- **A true reset cause must be latched.** Cause-checking boot paths read the
  reset-cause register; a causeless reset reads as an ordinary cold boot (or, on
  some kernels, as a sleep-exit that resumes from a stale save block) and hangs.
  The cause is latched through the SoC's reset-cause register via the sleep
  `DeepSleepWaker` (and, for CERF-initiated resets generally, `ResetCauseLatch`).
- **Reset-line silicon must reset on the wake.** A peripheral the board's reset
  line clears registers a `GuestCpuReset::RegisterResetListener` so it
  re-initialises at wake delivery (JIT thread). Skipping this leaves stale state
  that breaks the resume - e.g. a clock-control register still holding its
  power-off value is read back and re-enters sleep, or a stale input-device FIFO
  desyncs the driver's re-init handshake.

The wake banners "RESUMING" rather than "REBOOTING":
`ArmJit::SetResetPending(is_resume)` selects `GuestPowerNotifier::NotifyResume()`
vs `NotifyReboot()`; only the deep-sleep wake passes `is_resume`.

## Per-SoC / per-board wiring contract

To bring deep sleep up on a new SoC or board:

1. **Detect the power-down write** in the SoC peripheral that owns it (a
   power-manager force-sleep bit, a CP power-mode write, …) and call
   `emu_.Get<GuestDeepSleep>().Enter()`.
2. **Latch the wake cause.** The peripheral owning the reset-cause register
   implements `DeepSleepWaker::LatchSleepWakeCause()` (set the sleep/SMR cause
   bit) and registers via `GuestDeepSleep::RegisterWaker`.
3. **Reset reset-line silicon on wake** with `GuestCpuReset::RegisterResetListener`
   for each register/device the board's reset line clears.
4. **If the guest resumes at a saved vector** rather than the cold entry,
   implement `SleepResumeVectorProvider` (board-scoped) and register it with
   `GuestDeepSleep::RegisterResumeVectorProvider`. Its `Resume()` returns a
   `SleepResumeState`: `pc == 0` ⇒ fall through to the cold entry; otherwise the
   wake delivers the reset with PC = the resume vector, and `restore_mmu` ⇒ also
   reinstate cp15 control / TTBR0 / DACR so the MMU stays live across the resume
   (`ArmCpu::SetPendingResumeMmu`, which flushes the TLBs).

Suspend/resume serializes nothing - RAM stays live the whole time; the CPU is
merely parked.

## Resume mechanism: SoC baseline, OEM specifics

The SoC sets the baseline and the OEM builds the mechanism on top - the boundary
is blurry, so treat both layers as in scope:

- **SoC baseline (fixed by the chip):** sleep-exit is an internal reset that
  latches a sleep-mode reset cause (e.g. RCSR.SMR). That much is pure SoC and is
  what § "The wake IS a reset" models - the cause latch belongs to the SoC's
  reset-cause peripheral regardless of OEM.
- **OEM mechanism (built on top):** where the CPU/cp15 save block lives, whether a
  scratch register holds the resume vector, what the kernel StartUp checks, and
  what (if anything) the bootloader restores before handoff. These vary
  board-to-board on one SoC, and an OEM can add quirks on top of the baseline.

The `SleepResumeVectorProvider` seam exists for that OEM layer; pick the shape by
reading what the guest's own resume code does (decompile the boot / StartUp path):

- **In-kernel resume.** The kernel's StartUp checks the reset-cause register and
  branches to its own resume routine. CERF needs only to latch the cause (and, on
  some power managers, also set the status bit the kernel additionally gates on).
  No resume-vector provider is needed - the kernel finds its own saved block.
- **Kernel-saved resume vector.** Some kernels store the resume entry in a
  software-defined scratch register and re-enter there, skipping early boot code
  that would clobber the saved block. The board's provider returns that value as
  the resume PC.
- **CERF stands in for the bootloader.** When the bootloader CERF skips is the
  thing that reads the wake cause, restores cp15, and jumps to a saved resume
  address, the board's provider replays that: read the resume address + cp15 block
  the kernel left in RAM and return them as the resume PC with `restore_mmu` set.
  CERF skipping a bootloader silently drops any sleep handling it does that the
  kernel relies on - model that role rather than assuming the kernel is
  self-contained (see `agent_docs/boot_loaders.md`).

A scratch / "saved-state" register that one OEM uses as a resume vector is, on
another OEM (even the same SoC), a free-form software scratch or a checksum - it
is not a universal resume vector. Read the guest; do not assume.

## Suspend/resume is not hibernation

Two different mechanisms; conflating them causes real bugs:

- **Suspend/resume (this page)** is GUEST-driven: the guest powered itself down at
  runtime, RAM is live, and the guest's own resume code runs on wake. CERF halts
  the CPU and drives the wake. No machine state is serialized.
- **Hibernation** (`agent_docs/hibernation.md`, `cerf/state/hibernation.cpp`) is
  HOST-driven: CERF serializes the whole machine to a `.img` file and restores it
  later. The per-peripheral `SaveState` / `RestoreState` / `PostRestore` contract
  in `hibernation.md` is hibernation's; it does not apply to suspend/resume, which
  serializes nothing.

The two meet at exactly one point: a machine hibernated WHILE asleep restores with
`deep_sleep` set, so `GuestDeepSleep::OnFullRestore()` auto-wakes it (delivers the
wake, no dialog). Consequences:

- **Never re-post the recovery dialog on a hibernation restore.** Restoring a
  saved-asleep machine must auto-wake; prompting "Shut down CERF?" on restore is a
  bug (the user reopens a restored machine and is asked to shut it down).
- **A restore-into-deep-sleep IS the suspend/resume path**, running the same
  `DeliverWake` - not a separate hibernation defect to chase.

- `cerf/host/guest_deep_sleep.{h,cpp}`, `cerf/socs/guest_cpu_reset.{h,cpp}`,
`cerf/state/shutdown_dialog.{h,cpp}`; CPU halt/wake under `cerf/jit/`.
