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

## Two wake shapes - read the SoC before picking one

Sleep-exit is silicon behavior, and it takes one of two forms. Which one a SoC
implements is a fact to establish from its manual and its guest's own resume code,
never assumed:

- **Reset-on-wake.** Powering back up applies an internal CPU reset. The core
  re-enters at the reset/boot vector, reads the reset-cause register to
  distinguish sleep-exit from a cold boot, and runs the kernel/bootloader resume
  path from there. SA-1110 and PXA255 are this shape; it is the `DeepSleepWaker` /
  `SetResetPending` path below.
- **Clock-stop, resume in place.** The chip stops the CPU clock; the core holds
  its entire state and, when the clock restarts, continues at the instruction
  after the one it stopped on. No reset, no reset cause, no resume vector. The
  PR31x00 is this shape - TMPR3911 §12.2.4 (p.12-8): *"the CPU will remain
  suspended at its last state ... Then the CPU will resume the instruction
  execution from where it stopped."* CERF models it with `DeepSleepClockStop` +
  `GuestEngine::ExitDeepSleep()`: `DeliverWake()` applies what the silicon asserts
  on the power-up edge and un-halts the CPU at its halted PC.

**The SoC manual settles which shape a SoC has; the guest binary corroborates it.** Read
the power-down section first. It says either that the part stops the clock and holds state
(TMPR3911 §12.2.4), or that the power-down instruction *"enters reset status"* and recovery
*"begins the Cold reset exception sequence to access the reset vectors in the ROM space"*
(VR4102 UM §7.1.4 / VR4121 UM §8.1.4, of the HIBERNATE instruction). Then corroborate in the
guest: a reset-on-wake guest saves its whole CPU/CP0/peripheral context to RAM before the
power-down store and restores it from the reset entry, ending in a jump to the saved return
address - so the suspend call *appears* to return to its caller; a clock-stop guest simply
runs on.

**Live code after the power-down store does NOT prove the core was never reset.** Whether
the rails actually drop is a BOARD decision, so an OEM writes a routine that survives both
outcomes, and both VR41xx ROMs carry real code after their power-down instruction while
still being reset-on-wake. Modelling a reset-on-wake SoC as clock-stop resumes the guest in
place: none of the context it saved before the power-down store is restored, so it re-enters
sleep immediately and loops. Beware equally of a retention sentence that is conditional on
the core supply: a manual may say registers are retained in the low-power mode and, in the
same breath, that the mode exists so the core rails CAN be cut - and a battery handheld cuts
them. When the manual and a plausible reading of the guest disagree, the manual's power-down
section wins.

## Reset-on-wake

Where the wake IS a reset, it is delivered through the reset path:
`GuestDeepSleep::DeliverWake()` latches the wake cause and calls
`SetResetPending()`. The guest's OWN kernel/bootloader resume code - which runs at
the reset/boot entry - is what restores the live session.

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

To bring deep sleep up on a new SoC or board. Step 1 is common; steps 2-4 are the
reset-on-wake shape, step 5 the clock-stop shape. A SoC takes one shape or the
other, never both.

1. **Detect the power-down write** in the SoC peripheral that owns it (a
   power-manager force-sleep bit, a CP power-mode write, a simultaneous
   power-rail-enable clear, …) and call `emu_.Get<GuestDeepSleep>().Enter()`.
2. **Latch the wake cause.** The peripheral owning the reset-cause register
   implements `DeepSleepWaker::LatchSleepWakeCause()` (set the sleep/SMR cause
   bit) and registers via `GuestDeepSleep::RegisterWaker`.
3. **Reset reset-line silicon on wake** with `GuestCpuReset::RegisterResetListener`
   for each register/device the board's reset line clears.
4. **If the guest resumes at a saved vector** rather than the cold entry,
   implement `SleepResumeVectorProvider` (board-scoped) and register it with
   `GuestDeepSleep::RegisterResumeVectorProvider`. Its `ApplyPendingResume()`
   arms the next reset delivery through its own CPU service: an ARM board calls
   `ArmCpu::SetPendingResumeVector(pc)`, plus
   `ArmCpu::SetPendingResumeMmu(control, ttbr0, dacr)` (which flushes the TLBs)
   when the resume entry expects the MMU still live. Arming nothing leaves the
   reset at the cold entry. The `GuestEngine` seam is ISA-neutral
   (`jit.md` § The `GuestEngine` seam), so no cp15 crosses it - the board owns
   its own architecture's registers.
5. **On a clock-stop SoC**, steps 2-4 do not apply - there is no reset, no cause to
   latch and no resume vector. The peripheral owning the power register implements
   `DeepSleepClockStop::OnPowerUp()` and registers via
   `GuestDeepSleep::RegisterClockStopWaker`. `OnPowerUp()` applies exactly what the
   silicon asserts on the power-up edge (on the PR31x00, POWER_CTL PWRCS + VCCON,
   which §12.3.1 says hardware sets when ONBUTN is asserted while PWROK is high);
   `DeliverWake()` then calls `GuestEngine::ExitDeepSleep()`, which clears the halt
   so `JitRunner`'s park exits and `Run()` continues at the halted PC.
   **Silicon that loses power in the Suspend State still re-initialises.** No reset
   line fires on this path, so a `GuestCpuReset` reset listener does NOT run: a
   device on a power rail the suspend cuts (TMPR3911 §12.2.3: in the Suspend State
   "VSTANDBY and VCCDRAM are powered but VCC3 is not powered") must be re-initialised
   off the power-up edge instead. Miss it and the resumed guest's driver re-handshakes
   against a device still holding its pre-sleep state.

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
  the kernel left in RAM, then arm both via `ArmCpu::SetPendingResumeVector` and
  `ArmCpu::SetPendingResumeMmu`.
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
