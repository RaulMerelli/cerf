# CERF Subsystems

The host-side subsystem set is small. CE-side binaries (kernel, OAL, drivers,
userspace) run unmodified as guest code through the JIT - they
are not host subsystems. This page lists what CERF itself owns.

## CerfEmulator

The composition root. One C++ class, owned by `main.cpp`, owns every service.
Multi-instance by construction - two `CerfEmulator` instances inside the same
host process share nothing and can boot different device profiles side by
side. Services are resolved via `emu.Get<T>()`; statics and globals for
service state are forbidden.

### Service locator mechanics

- **Registration is one macro line per `.cpp`**: `REGISTER_SERVICE(Foo)`
  (resolvable only as `Foo`), `REGISTER_SERVICE_AS(Foo, Base)` (candidate
  for `Base`'s slot, also resolvable as `Foo` - the strategy pattern),
  `REGISTER_SERVICE_AS_FALLBACK(Foo, Base)` (wins only if no non-fallback
  candidate's `ShouldRegister` returned true). Boot then constructs every
  candidate, runs each slot's `ShouldRegister` to pick the single winner,
  then `EnsureReady`s **every** winner.
- **Every winning service's `OnReady` runs exactly once**, at boot,
  whether or not anything ever `Get<>`s it - the boot sweep `EnsureReady`s
  all winners, so a service that only wires itself up (registers MMIO,
  spawns a worker) still initializes with no external caller. `OnReady`
  is also lazy: a first `Get<Dep>()` from inside your `OnReady` runs
  `Dep::OnReady` before returning, so the graph self-orders on demand.
  Both paths guard with a mutex, so `OnReady` never runs twice. No init
  phase, no declared ordering, and no pre-warming - a bare
  `(void)emu_.Get<X>()` to force materialization is forbidden (`rules.md`).
- **`Get<T>()` fatals** if no candidate won the slot; **`TryGet<T>()`**
  returns `nullptr` for an optional dependency. Both lazily resolve +
  `EnsureReady`.
- **Misuse `CerfFatalExit`s with the types named** - two `ShouldRegister`
  true for one Base, a required slot with no winner, or a
  `ShouldRegister`↔`Get` cycle. No defensive checks around `Get<>` needed.
- **`ShouldRegister()`** may call `Get<>`/`TryGet<>` (same lazy path);
  idiom is `emu_.Get<BoardContext>().GetSoc() == SocFamily::X`.

- `cerf/core/cerf_emulator.h`, `cerf/core/service.h`

## Guest CPU JIT

Guest code runs through a block JIT. Every ROM binary -
`nk.exe` / `coredll.dll` / `gwes.exe` / `filesys.exe` / `device.exe` /
userspace EXEs / driver DLLs - is the original guest code, translated
to host x86 on the fly. `JitRunner` drives an abstract `GuestEngine`
service; the concrete engine for the board's CPU architecture implements
it, selected by `BoardContext::GetCpuArch()`. Per-SoC variation lives in
per-core strategy services selected by `GetSoc()`, never as
`if (soc == X)` branches in the JIT body.

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
`emu_.Get<BoardContext>().GetSoc() == SocFamily::X`. Chip-layer code
never knows which board it's on - only which chip.

The VA→PA placement map (`PageTableBuilder`) is **not** here, and the core
CPU strategies (`ArmProcessorConfig`, `CoprocEmitter`) split on a different
axis. VA→PA placement is a BSP/board choice (the OEMAddressTable differs per
board), so its concretes live under `cerf/boards/<board>/` and are selected by
`GetBoard()`. The core strategies are a CPU-arch property identical across every
board on that core, so their concretes live under `cerf/cpu/<core>/` and are
selected by `GetSoc()`. Gating a core strategy on
`GetBoard()` leaves every additional board on that SoC with no winner (a
second SA-1110 board re-stating the die's MIDR is the smell).

### `cerf/boards/<board>/` - one specific OEM board / BSP

One directory per supported board. Contains:

- `<board>_context.cpp` - the concrete `BoardContext` impl: reports the
  `Board`, `SocFamily`, `CpuArch`, and `RomPlacingMode` constants for that
  board, and registers when the configured `board_id` names it
- `<board>_page_table_builder.cpp` - the board's `PageTableBuilder` impl:
  the BSP OEMAddressTable VA→PA map, DRAM/flash backed regions, and the
  bootloader-handoff SP, used for ROM placement and pre-MMU boot
- board-only virtual peripherals - host-emulator notification channels,
  virtual DMA transports, folder-sharing helpers (peripherals that exist
  only because the board's BSP expects the emulator to provide them)
- BSP-specific config writers (e.g. `<board>_bsp_args.cpp` populating a
  DRAM struct the BSP reads on boot)

Concretes' `ShouldRegister` checks
`emu_.Get<BoardContext>().GetBoard() == Board::X`. A board's BoardContext
is the only thing that has to know its board name; everything else just
asks "am I on board X".

### `cerf/peripherals/<vendor>_<part>/` - off-chip silicon shared across boards

One directory per off-chip IC family. Today: `cirrus_pd6710/` (PCMCIA
controller), `amd_am29lv800bb/` (NOR flash). Tomorrow's additions go in
new sibling directories (e.g. `davicom_dm9000/` for the DM9000 NIC IC).

Concretes' `ShouldRegister` checks a board-list:

    auto b = emu_.Get<BoardContext>().GetBoard();
    return b == Board::X || b == Board::Y;

The list grows when a new board adopts the same part - the part file is
never duplicated, and the part directory is the single source of truth
for what the IC does.

The `cerf/peripherals/` root (not under any vendor subdir) also holds
the abstract `Peripheral` base (`peripheral_base.{h,cpp}`) and the
MMIO router (`peripheral_dispatcher.{h,cpp}`). All peripheral-domain
code - framework and concretes - lives in this one tree.

### Trees vs bases

Abstract bases (`BoardContext`, `PageTableBuilder`,
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
  insert submenu), `cerf/peripherals/serial_pccard/` (16550 serial /
  modem card - the PC-card consumer of the Serial stack below).

  **A card releases its pins in `PowerOff` / `OnShutdown`, NEVER in a
  destructor.** Nothing ejects a card at shutdown - it dies inside its
  controller's destructor, when that controller's own members are already
  gone, so a line dropped from a card dtor reaches into freed state. And
  **no Vcc means a card drives no pin, its interrupt included** (the
  socket floats its data lines on the same rule): a card with its own
  host or network thread gates its interrupt on power, or it strands a
  line on a socket it has already left.
- **`PcmciaSlot`** (`pcmcia_slot.{h,cpp}`) - one physical socket; owns
  card lifetime + bus serialization, IS the HostWidget with the
  universal Insert/Eject/Eject-and-insert menu. The owning controller
  implements **`PcmciaSlotHost`** (card-detect / IRQ callbacks) and
  routes guest accesses to the slot's Read/Write surface.
  **Card-detect and Vcc (`HasCard` / `IsPowered`) are lock-free reads of
  an atomic pin word, published under the bus lock - keep them that way.**
  A controller reads them from inside its own lock, while eject runs bus
  lock -> card -> IRQ callback -> that same controller lock: a pin
  accessor that took the bus lock closes an AB-BA against the UI thread.
  Every other slot entry point is a bus transaction, and a host calls it
  with its own lock released.
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

## Serial

The serial stack any UART can sit behind - the same endpoints serve an
on-SoC UART and a serial PC card, so a board gains ActiveSync / dial-up
by implementing one interface. Split line/personality:

- **`SerialLine`** (`cerf/peripherals/serial/serial_line.h`) - abstract
  UART surface an endpoint drives: push RX, read the guest's line config
  (baud/framing), raise modem-status inputs, drain callbacks. Concretes:
  **`Serial16550`** (the PC16550D model the ROM's own `ser16550` MDD/PDD
  drives unmodified) and any SoC UART peripheral that implements it.
- **`SerialEndpoint`** (`serial_endpoint.h`) - the "personality" behind a
  line: consumes guest TX, reacts to DTR/RTS, pushes RX + modem status
  back. Concretes: **`HostSerialForward`** (bridges to a real host COM
  port - overlapped I/O, baud-faithful TX pacing) and
  **`ModemPersonality`** (AT command set -> **`PppTerminator`** ->
  libslirp). One endpoint per attach point, owned by whatever holds the
  line.
- **`SerialCradle`** (`serial_cradle.{h,cpp}`) - the HostWidget for an
  on-SoC UART, mirroring `PcmciaSlot`'s insert/eject flow and sharing its
  card menus, so plugging a modem into a board's serial port looks like
  inserting a card. Owned by the UART peripheral, not a Service.
- `cerf/peripherals/serial_pccard/` holds ONLY the PC-card consumer; the
  framework above is board-neutral and lives in `cerf/peripherals/serial/`.

Wiring a board's stock UART = implement `SerialLine` on the UART
peripheral (RX path + an RX interrupt + baud from its divisor + modem
status on whatever pins the board wires), own a `SerialCradle`, and
supply a per-board seam naming the modem pins. Take those pin numbers
from the board's own serial driver, never a guess.

- `cerf/peripherals/serial/`

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
concretes (strategy pattern, selected by `BoardContext`).

- **`HostWindow`** - the top-level window. Owns the dedicated UI thread
  (window + message pump live there, not the main thread), the menu, and
  auto-resize-to-guest. The SoC LCD service calls `OnLcdEnabled` on the
  guest panel-enable edge to size the window to the guest surface.
  - `cerf/host/host_window.{h,cpp}`

- **`HostCanvas`** - the child window for the drawable area. Owns the
  **tabs** (`Tab::Boot` = boot screen, `Tab::Hw` = hardware text console,
  `Tab::Framebuffer` = the live guest framebuffer, `Tab::MemoryVisualizer` =
  dev), the viewport mode (Original / Aspect / Stretch, optional antialias),
  the scrollbars, and the single host-pixel↔guest-surface coordinate transform
  (`HostToGuest`) so taps land on the rendered image. The startup tab is
  `DeviceConfig.start_tab` (`--tab=boot|hw|fb`); on the first presented guest
  frame it auto-switches to `Tab::Framebuffer` unless the user has picked a tab.
  `Tab` is an alias of the core `CanvasTab` enum (so core config can name the
  startup tab without depending on the host layer). Publishes the atomic
  guest-surface dimensions the touch sampler reads. - `cerf/host/host_canvas.{h,cpp}`

- **`FrameRenderer`** (abstract) - `RenderInto(dib_bgra32, w, h)` fills a
  BGRA32 guest-surface DIB; `HostSizeFor` lets a rotating renderer swap
  width/height. Concretes live with the hardware that produces the frame:
  per-SoC LCD/DSS/IPU renderers under `cerf/socs/<chip>/`, board renderers
  under `cerf/boards/<board>/`, and the guest-additions virtual framebuffer
  under `cerf/peripherals/cerf_virt/`. - `cerf/host/frame_renderer.h`

- **`HwScreen`** - the hardware text console behind the `Tab::Hw` tab: the
  bounded text-mode RX/TX line buffer for guest UART / OEM-debug output and
  CERF's own notices (power events, save/restore progress). `AddLine` appends a
  line; `RenderInto` draws the scrolling log over the `BootBar`.
  - `cerf/host/hw_screen.{h,cpp}`

- **`BootScreen`** - the CERF-logo boot animation behind the `Tab::Boot` tab,
  plus the `BootBar`. Time-driven off the 60 Hz present loop (no thread);
  `Restart` (guest reboot / deep-sleep wake) and `OnFramebufferLatched` are its
  cross-thread control hooks.
  - `cerf/host/boot_screen.{h,cpp}`

- **`BootBar`** - the bottom CPU-activity bar shared by the Boot Screen and
  Hardware Screen tabs: a scrolling strip advanced off the host animation clock,
  so it freezes when emulation is paused. - `cerf/host/boot_bar.{h,cpp}`

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

- **`PointerRouter` / `PointerWidget`** - the pointer-source registry and
  host-mouse funnel, mirroring `KeyboardRouter`. Pointing-device sources (a
  board's touch / mouse, the guest-additions absolute pointer) self-register;
  the router forwards host mouse messages to the single active source, chosen
  by highest priority at boot. When more than one is registered, the
  `PointerWidget` status-bar widget switches the active source - some apps
  (calibrators) read the stock touch/mouse stream directly - and persists the
  choice across hibernation. On a board whose stock pointer is a relative
  mouse, the host-capture mouse lock engages only while that source is active.
  - `cerf/host/pointer_router.{h,cpp}`, `cerf/host/pointer_widget.{h,cpp}`

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

Always-built developer facility for hanging in-host C++ handlers off guest PC
addresses and per-`Run` iteration ticks, so bug-specific diagnostics never
pollute permanent code. Hot paths are zero-overhead when no traces are
registered (empty-container short-circuit). Hook surfaces: `OnPc` /
`OnPcFiltered` (per-instruction, the filtered form taking a fire-time process
predicate), compiled in every configuration, and `OnRunLoopIter` (dev-only).
Handlers read guest memory through `TraceContext::ReadVa8 / 16 / 32`
(`GuestEngine::PeekGuestVa`), with no MMU side effects.

Usage - picking a hook VA, per-process filtering, when to trace vs `LOG`,
adding a device trace file - is in [agent_docs/debugging.md](debugging.md)
§ TraceManager.

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
(the way `BoardContext` does); CRC/bundle gating stays in diagnostics, where
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
- `cerf.json` - optional per-device configuration; every field is optional and
  `cerf.exe` runs without the file. The one case it is genuinely required is a
  multi-partition / multi-file bundle (e.g. a ROM plus a separate EEPROM image
  present together): the `rom` block then names which file boots and which is
  data, so CERF does not have to guess. Everything else it carries is a user
  convenience - the `meta` block is launcher-only metadata (display name,
  board, SoC, OS, year), and `board` / `network` / `rom` overrides let a user
  pre-bake options they would otherwise pass through the launcher or CLI each
  time (a custom screen resolution on boards that support one or for a
  guest-additions boot, force-flipping a flag). A missing file means CERF uses
  `DeviceConfig` defaults plus CLI overrides.

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

## CE Apps - CERF-built CE binaries

`ce_apps/<name>/` directories build small Windows CE EXEs and DLLs
against the per-CE-era SDKs in `references/WindowsCE-Build-Tools/`
(`ce3-oak` … `ce7-oak`), per guest architecture. Each
directory has a `main.c` and a one-line `build.ps1` that delegates to
`tools/build_ce_app.ps1`; the top-level `build.ps1` walks every
`ce_apps/*/build.ps1` after msbuild succeeds. Outputs land at
`build/<Config>/Win32/ce_apps/<arch>/`. It hosts the `cerf_guest`
guest-additions display driver alongside sample binaries; it is the home
for any CERF-built CE binary.
