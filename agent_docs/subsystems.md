# CERF Subsystems

The host-side subsystem set is small. CE-side binaries (kernel, OAL, drivers,
userspace) run unmodified as ARM code through the JIT - they are not host
subsystems. This page lists what CERF itself owns.

## CerfEmulator

The composition root. One C++ class, owned by `main.cpp`, owns every service.
Multi-instance by construction - two `CerfEmulator` instances inside the same
host process share nothing and can boot different device profiles side by
side. Services are resolved via `emu.Get<T>()`; statics and globals for
service state are forbidden.

- `cerf/core/cerf_emulator.h`, `cerf/core/service.h`

## ARM JIT

ARM machine code runs through a block JIT. Every ROM binary -
`nk.exe` / `coredll.dll` / `gwes.exe` / `filesys.exe` / `device.exe` /
userspace EXEs / driver DLLs - is the original ARM code, translated
to host machine code on the fly. Both ARM-mode and Thumb-mode go
through the same `ArmJit` service; there is no per-SoC `ArmJit`
strategy. Per-SoC variation (PC store offset, base-restored-abort
model, cache-line size, MIDR/CTR values, coprocessor emit shape)
lives in `ArmProcessorConfig` and `CoprocEmitter` strategies
selected by SoC family - never as `if (soc == X)` branches inside
the JIT body.

The JIT subsystem is its own service constellation under `cerf/jit/`:
`ArmJit`, `ArmCpu`, `ArmMmu`, `ArmDecoder`, `ArmProcessorConfig`,
`CoprocEmitter`, `ArmCp15SctlrHandler`, `JitRunner`. Full design
notes - the `place_fn` contract, the pinned-register dispatcher,
the compile pipeline, the trampoline pattern, FCSE fold, shadow
stack, cross-thread interrupt delivery, SEH fault filter - live at
[agent_docs/jit.md](jit.md).

- `cerf/jit/`, [agent_docs/jit.md](jit.md)

## Per-chip / per-board / per-part strategies

CERF splits per-impl code across three orthogonal trees, picked by the
nature of the thing being implemented.

### `cerf/socs/<chip>/` - on-die silicon

One directory per SoC family (S3C2410 today; PXA27x, OMAP3530, SA-1110,
Poseidon, … added when their boards land). Contains:

- per-peripheral `<chip>_*.cpp` - UART, INTC, GPIO, RTC, timer, watchdog,
  clock/power, memory controller, LCD controller, NAND controller, IIS,
  etc.

Concretes' `ShouldRegister` checks
`emu_.Get<BoardDetector>().GetSoc() == SocFamily::X`. Chip-layer code
never knows which board it's on - only which chip.

The VA→PA placement map (`PageTableBuilder`) is **not** here, and the core
CPU strategies (`ArmProcessorConfig`, `CoprocEmitter`) split on a different
axis. VA→PA placement is a BSP/board choice (the OEMAddressTable differs per
board), so its concretes live under `cerf/boards/<board>/` and are selected by
`GetBoard()`. The core strategies are a CPU-arch property identical across every
board on that core, so their concretes live under `cerf/cpu/<arch>/` and are
selected by `GetSoc()`. Gating a core strategy on
`GetBoard()` leaves every additional board on that SoC with no winner (a
second SA-1110 board re-stating the die's MIDR is the smell).

### `cerf/boards/<board>/` - one specific OEM board / BSP

One directory per supported board. Contains:

- `<board>_detector.cpp` - the concrete `BoardDetector` impl (heuristically
  fingerprints the ROM bundle by a board-unique driver-blob signature in
  the TOC; reports `Board` and `SocFamily` constants for that board)
- `<board>_page_table_builder.cpp` - the board's `PageTableBuilder` impl:
  the BSP OEMAddressTable VA→PA map, DRAM/flash backed regions, and the
  bootloader-handoff SP, used for ROM placement and pre-MMU boot
- board-only virtual peripherals - host-emulator notification channels,
  virtual DMA transports, folder-sharing helpers (peripherals that exist
  only because the board's BSP expects the emulator to provide them)
- BSP-specific config writers (e.g. `<board>_bsp_args.cpp` populating a
  DRAM struct the BSP reads on boot)

Concretes' `ShouldRegister` checks
`emu_.Get<BoardDetector>().GetBoard() == Board::X`. A board's BoardDetector
is the only thing that has to know its board name; everything else just
asks "am I on board X".

### `cerf/peripherals/<vendor>_<part>/` - off-chip silicon shared across boards

One directory per off-chip IC family. Today: `cirrus_pd6710/` (PCMCIA
controller), `amd_am29lv800bb/` (NOR flash). Tomorrow's additions go in
new sibling directories (e.g. `davicom_dm9000/` for the DM9000 NIC IC).

Concretes' `ShouldRegister` checks a board-list:

    auto b = emu_.Get<BoardDetector>().GetBoard();
    return b == Board::X || b == Board::Y;

The list grows when a new board adopts the same part - the part file is
never duplicated, and the part directory is the single source of truth
for what the IC does.

The `cerf/peripherals/` root (not under any vendor subdir) also holds
the abstract `Peripheral` base (`peripheral_base.{h,cpp}`) and the
MMIO router (`peripheral_dispatcher.{h,cpp}`). All peripheral-domain
code - framework and concretes - lives in this one tree.

### Trees vs bases

Abstract bases (`BoardDetector`, `PageTableBuilder`,
`Peripheral`) live next to their consumers (`cerf/boards/`, `cerf/core/`,
`cerf/cpu/`, `cerf/peripherals/`), not under any per-impl tree.

Adding or removing a chip / board / vendor-part touches exactly one
directory. Splitting one impl's pieces across multiple trees (chip
pieces in board dir, board pieces in chip dir) is the wrong axis and is
itself the tech-debt shape this layout exists to prevent.

- `cerf/socs/`, `cerf/boards/`, `cerf/peripherals/`

## PCMCIA

16-bit PC Card emulation, split framework/controller/card:

- **`PcmciaCard`** (`cerf/peripherals/pcmcia/pcmcia_card.h`) - abstract
  card: attribute/common/IO surface, PowerOn/Off, SocketReset, optional
  `BuildCardMenu`. Cards are plain objects (NOT Services), created by
  `PcmciaCardCatalog::Create()` and owned by their slot; one type may
  occupy two slots at once. Concretes:
  `cerf/peripherals/realtek_rtl8019/` (NE2000 NIC),
  `cerf/peripherals/compactflash/` (PC Card ATA + FAT32 image builder +
  insert submenu).
- **`PcmciaSlot`** (`pcmcia_slot.{h,cpp}`) - one physical socket; owns
  card lifetime + bus serialization, IS the HostWidget with the
  universal Insert/Eject/Eject-and-insert menu. The owning controller
  implements **`PcmciaSlotHost`** (card-detect / IRQ callbacks) and
  routes guest accesses to the slot's Read/Write surface.
- **`PcmciaCardCatalog`** (`pcmcia_card_catalog.{h,cpp}`) - the
  insert-menu card registry; a new card type = one `PcmciaCard`
  subclass + one catalog entry.
- **`PcmciaSpaceRouter`** (`pcmcia_space_router.{h,cpp}`) - the shared
  SA-1110/PXA255 static-window PC Card space decode (PA 0x20000000;
  socket/region bits). Controllers call `ProvideSockets`; never
  re-implement this decode per SoC.

Wiring a new board = a socket controller in the proper tree
(chip/board/vendor-part) that owns `PcmciaSlot` instances, implements
`PcmciaSlotHost`, registers the slots with `HostWidgetRegistry`, and
drives `SetPowered` from its power register - see `cirrus_pd6710/`
(DevEmu), `intel_sa1111/sa1111_pcmcia.cpp` (Jornada),
`ipaq_gen1_pcmcia_sleeve.cpp`, `falcon_pcmcia.cpp`.

- `cerf/peripherals/pcmcia/`

## CPU reset & cold boot

**`GuestCpuReset`** is the funnel for every CERF-initiated CPU reset
(host reset actions, watchdog expiry) - never call bare
`ArmJit::SetResetPending`. The SoC peripheral owning the reset-cause
register implements `ResetCauseLatch` (warm / cold / watchdog) so the
re-entered guest boot path reads a true reset cause, and peripherals
whose silicon resets with the system reset line (e.g. a drive
re-presenting its power-on diagnostic signature) register reset
listeners that run at reset delivery on the JIT thread. A board that
skips this wiring boots fine but hangs on guest reboot: cause-checking
startups take the sleep-resume path, and warm peripheral state fails
driver re-probes.

- `cerf/socs/guest_cpu_reset.{h,cpp}`

**`GuestColdBoot`** implements hard reset (cold boot): at reset
delivery it wipes every volatile RAM region (flash survives), replays
registered boot-time guest-RAM writes in registration order, and
flushes the translation cache. Every service that writes guest RAM
during `OnReady` (ROM placement, BSP_ARGS blocks, image injection)
MUST register a replay - or a byte-exact `RecordPatch` when the
producing computation allocates and must not re-run - otherwise its
bytes are silently absent after every hard reset on that board.

- `cerf/boot/guest_cold_boot.{h,cpp}`

## Host window & presentation

The Win32 window, its drawable area, and the render/input plumbing that
connects the host UI to the guest. All are `Service`s; the renderer,
touch, and keyboard pieces are abstract bases with per-SoC/per-board
concretes (strategy pattern, selected by `BoardDetector`).

- **`HostWindow`** - the top-level window. Owns the dedicated UI thread
  (window + message pump live there, not the main thread), the menu, and
  auto-resize-to-guest. The SoC LCD service calls `OnLcdEnabled` on the
  guest panel-enable edge to size the window to the guest surface.
  - `cerf/host/host_window.{h,cpp}`

- **`HostCanvas`** - the child window for the drawable area. Owns the two
  **tabs** (`Tab::Hw` = hardware/boot/debug-console screen, `Tab::Framebuffer` =
  the live guest framebuffer), the viewport mode (Original / Aspect /
  Stretch, optional antialias), the scrollbars, and the single
  host-pixel↔guest-surface coordinate transform (`HostToGuest`) so taps
  land on the rendered image. Publishes the atomic guest-surface
  dimensions the touch sampler reads. - `cerf/host/host_canvas.{h,cpp}`

- **`FrameRenderer`** (abstract) - `RenderInto(dib_bgra32, w, h)` fills a
  BGRA32 guest-surface DIB; `HostSizeFor` lets a rotating renderer swap
  width/height. Concretes live with the hardware that produces the frame:
  per-SoC LCD/DSS/IPU renderers under `cerf/socs/<chip>/`, board renderers
  under `cerf/boards/<board>/`, and the guest-additions virtual framebuffer
  under `cerf/peripherals/cerf_virt/`. - `cerf/host/frame_renderer.h`

- **`HwScreen`** - the hardware screen behind the `Tab::Hw` tab: the bounded
  text-mode RX/TX line buffer for guest UART / OEM-debug output and CERF's own
  notices (power events, save/restore progress). `AddLine` appends a line,
  `RenderInto` orchestrates the frame (delegating the logo/boot animation to
  `HwBootAnimation`, then the scrolling log + boot bar). - `cerf/host/hw_screen.{h,cpp}`

- **`HwBootAnimation`** - the `HwScreen` boot-visual owner: the CERF-logo
  fade-in/hold/fade-out intro, the optional OEM-logo fade-in ("Starting
  <board>…", logo + short name from `BoardDetector::GetBootLogoResource` /
  `GetShortBoardName`), and the held / dimmed-center final states. Time-driven
  off the 60 Hz present loop (no thread); `Restart` (guest reboot →
  "Restarting…"), `Abort` (restore failure), and `OnFramebufferLatched`
  ("Switched to LCD") are its cross-thread control hooks.
  - `cerf/host/hw_boot_animation.{h,cpp}`

- **`TouchInput`** (abstract) - `OnPenDown/Move/Up` + `OnCaptureLost` in
  guest-surface coordinates; the board's touch peripheral concrete turns
  them into guest pen samples. Concretes under `cerf/boards/<board>/`.
  - `cerf/host/touch_input.h`

- **`KeyboardInput`** (abstract) - one keyboard source: `OnHostKey(vk, key_up)`
  plus `SourceName` / `SourcePriority`. Concretes (a board's keyboard under
  `cerf/boards/<board>/`, the guest-additions keyboard under
  `cerf/peripherals/cerf_virt/`) self-register with `KeyboardRouter`.
  - `cerf/host/keyboard_input.h`

- **`KeyboardRouter`** - the keyboard-source registry and host-key funnel.
  `KeyboardInput` concretes self-register from `OnReady`; the router forwards
  host keys to the single active source, chosen by highest `SourcePriority` at
  boot. When more than one source is registered, the `KeyboardWidget` status-bar
  widget switches the active source and persists the choice across hibernation.
  - `cerf/host/keyboard_router.{h,cpp}`

- **`HostInputCapture`** - the low-level keyboard hook + capture toggle (so
  the guest receives keys the host shell would otherwise eat); forwards to
  `KeyboardInput` and synthesizes Ctrl-Alt-Del. Installed/removed on the UI
  thread. - `cerf/host/host_input_capture.{h,cpp}`

- **`HostStatusBar`** - the bottom status bar. Renders the
  `HostWidgetRegistry`'s ordered widget set (icons + per-icon tooltips,
  left-click → primary action, right-click → declarative popup); the
  capture/lock indicator is itself one such (host-owned) widget.
  - `cerf/host/host_status_bar.{h,cpp}`

- **`HostWidget` / `HostWidgetRegistry`** - the status-bar + Actions-menu
  widget framework. `HostWidget` is an abstract, **non-`Service`** interface
  (so a `Peripheral`, which already derives `Service`, can implement it
  without a diamond) that any service implements to declare a host-UI
  presence: a custom GDI icon, tooltip, left-click action, declarative
  right-click menu (replicated into the Actions menu), hot-path-safe RX/TX
  activity dots, an `IsEnabled()` grayscale seam, and a `WidgetGroup`
  ordering key (the terminal `InputControl` group pins rightmost).
  Implementers self-register with `HostWidgetRegistry` from `OnReady`, the
  same way peripherals self-register with `PeripheralDispatcher`;
  `HostStatusBar` renders the ordered set and concretes follow the three-tree
  rule (`cerf/socs|boards|peripherals/`). Reach for it whenever a peripheral
  or board has user-visible state (RX/TX, enabled/disabled) or a
  configuration/toggle surface. - `cerf/host/host_widget*.{h,cpp}`

- **`HostScreenshot`** - screenshot + clipboard capture of the live guest
  surface (via `HostCanvas::CaptureGuestSurface`, 1:1).
  - `cerf/host/host_screenshot.{h,cpp}`

## TraceManager

Always-built developer debugging facility for putting in-host C++ handlers
behind specific guest PC addresses, guest memory addresses, and per-JIT-Run
iteration ticks - without polluting permanent code with bug-specific
diagnostics. Hot paths are zero-overhead when no traces are registered
(empty-container short-circuit). The PC-trace surface and CRC gate
compile in every configuration; the RunLoop-iter surface is dev-only.
Production registers only the kernel-debug (`nkdbg/`) hooks; dev
additionally registers bug-specific trace files.

Two hook surfaces:

- **PC trace** (`OnPc(runtime_va, handler)`) - handler fires once per
  execution of the guest instruction at `runtime_va`. Implemented in
  the JIT block compiler (`cerf/jit/arm_jit_generate_code.cpp`): for
  each decoded instruction at compile time, if
  `TraceManager::HasPcTrace(pc)` returns true, the compiler emits an
  x86 `CALL` to `ArmJit::TraceDispatchPcHelper` immediately before the
  instruction's `place_fn` emit; the helper routes through
  `TraceManager::DispatchPc(pc, regs, cpsr)`. The trace call is
  placed inside the same cond-guarded region as the instruction's
  emit - for conditional ARM instructions, the trace fires iff the
  condition is true at runtime, the same condition under which the
  guarded instruction itself executes.
- **RunLoop iter** (`OnRunLoopIter(handler)`, dev builds only) - handler fires after each
  `ArmJit::Run()` return in `JitRunner::RunLoop`. Used for value-change
  pollers and one-shot startup audits.

**There is no OnRead / OnWrite memory-watch primitive.** A prior
design exposed one and it was a footgun: every watched VA forced
every memory access on the containing 4 KB page through an MMU walk
+ PeripheralDispatcher + EmulatedMemory + dispatch callback chain
instead of the JIT's GuestTlb fast path, slowing those accesses
30-50×. The cumulative slowdown shifted guest IRQ delivery
alignment relative to the kernel scheduler, and that shift CREATED
Heisenbug-shaped races / deadlocks that did NOT exist in production
CERF. The wm5_smdk2410_devemu boot-stall investigation burned 20+
human hours chasing one of these - production builds (trace files
excluded) reproduced zero stalls in 15 runs, dev builds with one
OnWrite on a thread-descriptor page stalled 20-40% of runs.

"Move the watch to a less-hot page" doesn't solve it - any
page-exclusion shifts the timing race somewhere else where it can
manifest as a different false bug. There is no safe page. The
mechanism itself is what's unsafe.

To observe a memory write, hook `OnPc` at the writer instruction PC
and read the freshly-written value via `c.ReadVa8 / 16 / 32(va)` on
the `TraceContext` inside the handler (these go through
`ArmMmu::PeekDataTlb`, GuestTlb fast-path only, no side effects).
For values whose writer PC is unknown, poll via `OnRunLoopIter` (no
per-access overhead between `ArmJit::Run()` returns). For all
writers of a value, attach `OnPc` to each writer site individually.

`TraceContext` (passed to every handler) carries the 16 GPRs, CPSR, PC, and
a `CerfEmulator&` for service access. `ReadVa8 / 16 / 32` are read-only
GuestTlb-fast-path peeks (no MMU side effects, return `std::nullopt` for
pages not currently fast-path-mapped).

- `cerf/tracing/trace_manager.{h,cpp}`, `Trace` log channel.

## Device-specific trace files

`cerf/tracing/<bundle>/*.cpp` - one subdirectory per device bundle.
Each file is a small `Service` whose `OnReady` calls
`TraceManager::RegisterForBundle(<expected_crc32>, register_fn)`. The
closure runs iff the live bundle's CRC32 matches; otherwise the file
silently no-ops at runtime. `<bundle>/bundle.h` (or `wm5_bundle.h` etc.)
declares `constexpr uint32_t kBundleCrc32 = ...;` used by every trace file
in that directory.

The bundle CRC32 is computed by `TraceManager::OnReady` over the
concatenated `RomParserService::Loaded()[i].raw` bytes in load order. On
first boot for a new bundle, the log line `[TRACE] bundle CRC32 = 0xXXXX`
gives you the value to paste into the trace file's `bundle.h`.

`build.ps1 -Mode production` excludes the per-device trace files from the
build via a `<ClCompile Remove="tracing\*\**\*.cpp">` rule in
`cerf/cerf.vcxproj`, then re-includes `tracing\*\nkdbg\*.cpp` so the
kernel-debug hooks stay in production builds. The framework
(`cerf/tracing/trace_manager.{h,cpp}`) stays compiled; with no registered
traces, every hook is a single empty-container check.

**CRC32 / bundle gating is diagnostics-only - never runtime behavior.**
Because this per-device trace tree is stripped from production builds, any
emulation or board behavior placed behind a `RegisterForBundle` / bundle-CRC
gate compiles OUT of the production binary: it works in a dev build and is
silently dead for every user, with no error pointing at the absence. A CRC
also matches ONE exact ROM image, never a class - a board has many ROMs
(revisions, regions, generations, future user dumps), so CRC-gated behavior
needs an unbounded checksum list and any unseen ROM gets nothing. Behavior
that must hold for a class of ROMs uses a generalizing ROM-content fingerprint
(the way `BoardDetector` does); CRC/bundle gating stays in diagnostics, where
its single-image, dev-only nature is exactly correct.

The one production-built CRC-gated exception is the kernel-debug (`nkdbg/`)
hooks: they are OBSERVATION-ONLY (read guest debug text, emit it to the log +
HwScreen - never altering emulation or board behavior) and fail benignly (a
CRC mismatch installs no hook, costing only absent debug output, never a
misbehaving device). Anything that changes how the guest runs stays out of
CRC gates.

- `cerf/tracing/<bundle>/`

## Kernel debug output

`KernelDebugSink` (`cerf/tracing/kernel_debug_sink.{h,cpp}`) is the single
funnel for guest OS debug text. Every producer routes finished lines to it: a
live SoC/board UART or serial peripheral's TX, and a hook on a nulled OEM debug
sink (`cerf/tracing/<bundle>/nkdbg/`). It emits each line to the `Nkdbg` log
channel (`[NKDBG]`) and to the `HwScreen` debug console.

- `EmitLine(line, source, to_screen)` - the output primitive; `source` is an
  optional tag (e.g. `"UART1"`).
- `EmitChar(ch, buf, …)` - the common CRLF / hex-escape / cap accumulator over a
  caller-owned buffer (concurrent producers never share state).
- `EmitWideStringAt(ctx, va, …)` - read a guest wide string and emit it.

A UART/serial peripheral never open-codes `LOG` + `HwScreen::AddLine`; it calls
the sink. Register-access logging stays on its own SoC channel (e.g. `SocUart`)
and is gated like any other channel - only the assembled debug line is `Nkdbg`.

- `cerf/tracing/kernel_debug_sink.{h,cpp}`, `Nkdbg` log channel

## Bundled device tree

`bundled/devices/<name>/` is the input CERF reads at boot:

- A Windows CE ROM image, `*.nb0` or `*.bin`. CERF picks up whichever
  is present; the filename does not matter.
- `cerf.json` - per-device runtime configuration. The `meta` block
  identifies the device (display name, board name, SoC family, OS
  name + version, year - used by the launcher and status displays).
  Optional `board` / `network` / `rom` blocks override `DeviceConfig`
  defaults. A missing file means CERF uses `DeviceConfig` defaults
  plus CLI overrides.
- `cerf.json` is completely optional and has no value for `cerf.exe`.
- You can't use `meta` in `cerf.json` for `cerf.exe` for anything more
  serious than displaying device name beatified string - it's metadata.

`bundled/devices/` is synced via `./launcher`,
which downloads the public manifest and installs selected bundles.

Downloaded bundle directories and the local `manifest.json` are
ignored by Git - only those are copied to the release directory; users
run `launcher` locally. Never run `launcher` on your own.

For IDA debugging, the same `.nb0` / `.bin` is decomposed offline by
`tools/extract_bundles.py` (runs `references/extract-wince-rom`
against each ROM and copies any matching PDBs in) into
`references/extracted-roms/<dev>/<rom>/`. That tree is gitignored,
not consumed by CERF at runtime, and exists solely for IDA / static
analysis - see `agent_docs/debugging.md` § IDA discipline.

Build-time staging mirrors `bundled/**/*` into `build/<config>/Win32/**`
via the `CopyBundledFiles` MSBuild target (incremental, never deletes
destination files absent from the source set).

We are using `bundled/devices` locally because it is synced into 
`build\release\win32\devices` however regular users have launcher
inside build directory and sync the devices folder there.

## Launcher

`launcher/` is a standalone Python/tkinter GUI (packaged to `launcher.exe`
via PyInstaller), not a `CerfEmulator` service but a separate host-side
tool. It syncs public ROM bundles into `bundled/devices/` from a remote
manifest, authors each installed bundle's `cerf.json` from the manifest's
`cerf_json` and reconciles it on refresh, boots a selected device via
`cerf.exe`, and runs a release self-update check
against the repo-root `.last-release-version`. It owns the developer-editable
supported-boards / board-quirk list (`supported_devices.py`), whose per-board
`notes` surface in the side panel and extend a ROM's own `cerf.json` notes.

- `launcher/` (`launcher.py`, `bundles.py`, `operations.py`,
`supported_devices.py`)

## CE Apps - CERF-built ARM CE binaries

`ce_apps/<name>/` directories build small Windows CE ARM EXEs and DLLs
against the per-CE-era SDKs in `references/WindowsCE-Build-Tools/`
(`ce3-oak` … `ce7-oak`). Used as bundled samples; in v1 they drove the
test harness. Each directory has a `main.c` and a one-line `build.ps1` that
delegates to `tools/build_ce_app.ps1`; the top-level `build.ps1` walks
every `ce_apps/*/build.ps1` after msbuild succeeds. Outputs land at
`build/<Config>/Win32/ce_apps/<name>.{exe,dll}`. Today it hosts the
`cerf_guest` guest-additions display driver alongside sample
binaries; it is the home for any CERF-built CE binary.
