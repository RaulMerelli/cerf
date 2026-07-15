# Guest Additions

Opt-in (`--guest-additions`, off by default). At ROM load CERF replaces the
board's stock display driver with the universal `cerf_guest` driver and brings
up a set of host-integration features on top of it: host-framebuffer rendering,
host-accelerated blitting, a mouse-pointer cursor, keyboard injection, dynamic
screen resolution, shared host-folder storage, and a guest task manager. The subsystem spans host
C++ under `cerf/boot/` + `cerf/peripherals/cerf_virt/` and guest CE code under
`ce_apps/cerf_guest/` + `ce_apps/cerf_guest_stub/`.

## How cerf_guest is injected (the universal mechanism)

`cerf_guest` is larger than a typical victim ROM slot, must survive boards that
wipe DRAM at boot, and must not be rejected by Windows Mobile module
authentication. One mechanism satisfies all three on every ROM class and every
CE version. Two pieces are common to all ROMs:

- A **tiny stub** (`ce_apps/cerf_guest_stub/`) is injected as the victim
  display-driver module: the victim's module record is repointed at the stub,
  whose bytes are placed in dedicated memory, never squatted into the victim's
  own image. Keeping it tiny sidesteps the per-CE-version slot-size limits the
  full body would hit.
- The **full cerf_guest body** is delivered separately over a `cerf_virt` MMIO
  channel (`cerf/peripherals/cerf_virt/cerf_virt_guest_body.cpp`). The stub +
  body are staged per guest architecture under `ce_apps/<arch>/`, selected by
  `GuestAdditionsBinaries::ArchDir` (`cerf/boot/guest_additions_binaries.{h,cpp}`)
  off `GetCpuArch()` plus the ROM's CPU subtype. The stub pulls the body bytes
  and **manual-maps the PE itself** - VirtualAlloc + section copy + base reloc +
  coredll import bind + entry - so the kernel's verified loader never sees the
  unsigned body. This loads past WM5 / WM2003SE module authentication, gives each
  loading process its OWN body image (per-process by construction), and lives in
  CERF-owned memory a firmware RAM wipe can't reach.

Only **how the stub is placed into the ROM** differs by ROM class:

- **XIP / MultiXIP** (`cerf/boot/guest_additions_injector.cpp`) - the ROM carries
  a TOC of XIP modules, so the injector overwrites the victim module's TOC entry
  (its e32/o32/load offsets) to repoint it at the stub. The stub's e32/o32 records
  and section bytes live in a CERF-owned PA band in the `cerf_virt` window
  (`cerf/boot/cerf_injection_region.{h,cpp}`), exposed at a guest-unmapped
  static-window VA - a hole found by `PageTableBuilder::StaticWindowHole` over the
  board's `MappedVaSpans()` - and served by an MMU-walker overlay
  (`ArmMmu::ServeInjectionBand` at the L1-fault site); never the victim's own
  section bytes. On CE6/7 the stub runs in place from the band (a kernel-VA base
  makes the loader skip its section copy); on CE3/4/5 the loader copies it to a
  section-1 vbase (`GuestModulePlacer::ComputeVbase`), with the band sections
  flagged MEM_WRITE|SHARED so the loader takes the overlay-servable memcpy without
  a per-process slot-base fold on the device.exe carrier load.
- **IMGFS** (WM6+; `cerf/boot/imgfs_injector.cpp` + `ce_imgfs_patcher.{h,cpp}`) -
  IMGFS is a flash filesystem (an FTL over the NOR/NAND image), not an XIP TOC,
  so there is no slot to overwrite. The injector allocates fresh FTL flash pages
  (writing each page's logical-sector-map entry so imgfs.dll adopts it), writes
  the stub's `e32_rom` module header + per-section data into those pages, and
  repoints the victim's IMGFS **dirent** module-index / section-index records at
  the new pages. The SAME stub is injected; only the placement substrate is the
  FTL instead of the TOC.

Every path flags the stub's writable sections SHARED - via
`GuestModulePlacer::EffSectionFlags`, or directly alongside MEM_WRITE on the
CE3/4/5 XIP copy path - and the stub keeps its per-process state pid-keyed; see
§ The cross-process writable-state invariant for why.

The body's manual-map shape is a load-bearing contract: a single import DLL
(coredll), HIGHLOW-only relocations, no TLS. Rebuilding `cerf_guest` so it gains
a second import DLL, MOV32 relocations, or a TLS directory silently breaks the
stub's mapper.

coredll is imported **by name**, not by ordinal - high ordinals are per-CE-version
with no cross-version contract, names are the contract - and the stub resolves
each name with `GetProcAddress` in the loading process. The trap: a coredll
function that some target coredll does not export **by name** - exported by
ordinal only, or absent entirely - cannot be resolved, so the by-name manual-map
fails and the whole body never loads (Pocket PC 2000's coredll exports
`CeSetExtendedPdata` ordinal-only; the CE2.0 coredll lacks the CRT and the 64-bit
compiler helpers entirely; older coredlls are where this bites). Such a symbol
must be **defined locally in cerf_guest** rather than imported - a local
definition wins over the import-lib thunk, so no coredll import is emitted for it.
`cerf_ce2_crt.cpp` does this for the CE2.0 target: `memset` / `memcpy` /
`operator new` / `operator delete` and the compiler helpers `__ll_lshift` /
`__ll_div`, none of which that coredll exports. When adding a coredll import to
`cerf_guest`, confirm the **oldest** target coredll exports it by name; if not,
define it locally.

## Shared host storage (AFS FSD in device.exe)

A host folder is mounted into the guest as a filesystem, live-toggleable at
runtime, on every CE version. It registers directly on coredll's **AFS API-set
primitive** (`RegisterAFSName` + `CreateAPISet` + `RegisterAFS`) the way the
in-ROM `fatfs.dll` does - so it needs no `fsdmgr.dll` at runtime and works
uniformly CE3→CE7. The FSD MUST run in **device.exe** (the legitimate FSD host);
hosting it in gwes is illegal and corrupts cross-process loads. `cerf_guest`
reaches device.exe via a **driver-in-driver carrier**: it registers a `CDD_*`
stream driver and `ActivateDevice`s it, so device.exe loads the same module and
runs the FSD from `CDD_Init`. Guest side: `ce_apps/cerf_guest/cerf_fs_*.c` +
`cerf_driver_in_driver.cpp`. Host transport: the `ServerPB` peripheral
`cerf/peripherals/cerf_virt/cerf_virt_folder_share*` + `folder_share_*`.

## The cross-process writable-state invariant

The injected module is loaded by more than one process - gwes (display),
device.exe (the FSD carrier), and any DirectDraw-HAL client that loads the
display driver (`HALInit`). Whether the kernel gives each process its own
writable copy depends on the module's vbase: a **CE5/FCSE kernel folds a
`WRITE & !SHARED` section to a per-process slot copy only when the vbase is in a
slot / section-1 region; a module at a high
shared-region vbase keeps one physical `.data` shared across every loading
process.** This is why a low-vbase IMGFS victim (WM6.0) tolerated a full
kernel-loaded body but a high-vbase one (WM6.5) corrupted gwes's globals
cross-process - and the reason the per-process manual-mapped stub body exists.
Where the writable statics ARE shared, a per-process `VirtualAlloc` address
stored in such a static is meaningless in the other process. Two rules follow,
and both are mandatory for any injected guest module:

- **Per-process runtime state must be keyed by process id.** A flat static one
  process initializes is read with that process's value by the next, which then
  skips its own setup. (The debug-log channel and the stub's mapped-body +
  resolved-export table are both pid-keyed for this reason.)
- **Writable injected sections are flagged SHARED.** The CE loader gives a
  per-process copy only to `WRITE & !SHARED` sections; without a per-process
  DLL-RW reservation that copy folds to the bare slot base and faults the second
  loader. Flagging the section SHARED keeps it one shared copy, which pid-keyed
  runtime state then makes correct. This is one mechanism that works on FCSE and
  ASID kernels alike, with no per-board reservation.

Corollary: a placement/reservation mechanism that was correct when a module was
kernel-loaded becomes vestigial once the body is delivered by manual-map
instead - re-audit load-bearing helpers for vestigiality after any change to how
their inputs are produced, rather than assuming they are still required.

## The version / ROM-class axis

- **FCSE (CE ≤ 5)** - one shared page table; low VAs relocated per process by
  the cp13 FCSE PID. The shared-writable-statics hazard above is an FCSE
  property.
- **ASID (CE6/7)** - per-process page tables; the same injected module's
  writable data is per-process by construction.
- **ROM classes** - XIP single, MultiXIP, and IMGFS (WM6+). XIP / MultiXIP go
  through `guest_additions_injector.cpp`; IMGFS through `imgfs_injector.cpp`.

## Display driver + blit pipeline

The universal CERF display driver, injected into the guest ROM at load time by
`--guest-additions`. The body is CERF's own GPE/DDGPE/DDI scaffolding
(`ce_apps/cerf_guest/include/cerf_gpe.h`, `include/cerf_ddi.h`) plus the driver
built on it, linking `coredll` only. A per-version compatibility layer - the
DDI/DDHAL translation (`cerf_ddi_*.cpp`, `cerf_ddhal*.cpp`) and per-generation
ddraw headers
(`include/ddraw_ce5.h` / `ddraw_ce6.h` / `ddraw_wm.h`) - reshapes the
driver-interface data at the OS boundary so the single CE6-shaped driver runs
unmodified across CE2.0 → CE7 and Windows Mobile 5/6 (each OS sees its own
generation's shapes; the driver always sees CE6 shapes).

It is the guest-side partner of the host `cerf/peripherals/cerf_virt/` virtual
framebuffer + `gpe_cmd` accelerator channel: the driver routes blits over that
channel and the host performs them natively (`cerf_ddgpe_blt.cpp` `BltPrepare`
routing; `main.cpp` channel ABI). It owns the universal display path -
host-framebuffer rendering, host-accelerated blits, a DirectDraw HAL, dynamic
resolution, and the cursor / keyboard / shared-storage / task-manager transport.

**HW means HW.** The `cerf_guest` accelerator must handle every blit class
(copy, fill, format-convert, palette, masked text, transparency, ROP, …)
host-side through the virtual accelerator channel. Routing a blit class to the
guest's software render path (GPE `EmulatedBlt`) because the case is complex,
format-converting, masked, or transparent is forbidden and is a bailout - "let
it stay software" is never the fix. The software path is an extreme edge
reserved only for genuinely un-accelerable inputs (a guest page that cannot be
translated), never a design choice for a hard blit; a blit shape that renders
correctly under software but is declined to it is unaccelerated work, not a
finished feature.

**Display colour model.** Direct-colour on CE3+/CE2.11 (≥16bpp), but **CE2.0
must be 8bpp-indexed**: its gwes creates only `PAL_INDEXED`, so a ≥16bpp
framebuffer leaves `hpalDefault` = 0 and faults gwes (Exception 002) in display
init. A CE2.0 board forces it with `BoardContext::GetGuestAdditionsColorDepth()`
= 8; the indexed path publishes the palette over the `cerf_virt` palette channel
and the host expands index→RGB on scanout. Selected by framebuffer bpp, never OS
version (CE2.11 is already ≥16bpp).

**Floor is CE 2.0; CE 1.0 GA is impossible, not unimplemented.** GA replaces the
guest's *loadable* display driver - the DDI `DrvEnableDriver` the body
implements, present from CE 2.0. CE 1.0 gwes is the whole graphics stack: it
renders every pixel into the physical LCD aperture itself (GDI is an in-gwes
apiset), exports no `DrvEnableDriver`, and its only external display hook is an
optional dirty-rectangle flush (`DEVICEMAP\DISPLAY\DriverName` →
`DirtyRectInit`/`DirtyRectUpdate`), not a renderer. With no replaceable driver
GA's core cannot attach; the CE 1.0 desktop reaches the host via the board's
native LCD aperture. Do not attempt CE 1.0 GA.

## Keyboard injection

Host keystrokes reach the guest through the same mechanism as the mouse-pointer
pump, with `keybd_event` in place of `mouse_event`. Both are coredll exports
that trap into the same GWES API set (API-set 81) and are present unchanged on
every CE version (CE 2.11 → CE 7), so one guest binary drives keyboard input on
every guest OS, with no per-version code and no guest-installed driver.

- Host channel: `cerf/peripherals/cerf_virt/cerf_virt_keyboard.{h,cpp}` +
  `cerf_virt_keyboard_regs.h`. Keys are edges (down/up) that must arrive once
  and in order, so the channel is a ring buffer rather than the pointer's
  level / last-value-wins registers: the host appends one entry per host-key
  transition and bumps the write sequence after the entry store; the guest
  drains entries by index.
- Guest pump: `ce_apps/cerf_guest/cerf_keyboard_pump.cpp`, a thread started
  from the display driver's `DrvEnablePDEV` beside the pointer / resize / task
  pumps. It resolves `keybd_event` via `GetProcAddressW` and replays each ring
  entry; GWES routes it to the focused window and runs the active keyboard
  layout to produce WM_KEYDOWN / WM_KEYUP + WM_CHAR.

The guest-additions keyboard registers as one source in the host
`KeyboardRouter` (`cerf/host/keyboard_router.{h,cpp}`) with the highest source
priority, so it is the active source whenever guest additions are on. The
router is board-agnostic host input and is not itself part of guest additions -
see `subsystems.md`.

The guest-additions mouse registers the same way - one source in the host
`PointerRouter` (`cerf/host/pointer_router.{h,cpp}`) at the highest source
priority, so the GA absolute pointer is the active device whenever guest
additions are on. The `PointerWidget` switches it for the board's stock
pointer(s) - needed because some apps (e.g. calibrators) read the stock
touch/mouse stream directly. On a board whose stock pointer is a relative mouse,
the host-capture mouse lock engages only while that source is active. Router and
widget are board-agnostic host input, not part of guest additions - see
`subsystems.md`.

## Task manager - host UI + cerf_virt channel + guest pump

The guest task manager (Guest Additions status-bar widget → non-modal host
window) lists, kills, and switches to guest processes and runs guest
executables. Three pieces: the MMIO command/response channel
`cerf/peripherals/cerf_virt/cerf_virt_task_manager.{h,cpp}` (+ `_regs.h`
contract - host publishes one command at a time via gen bump; guest answers with
response regs + kick; LIST rows stream guest→host one record at a time through a
register window), the host window + widget
(`cerf/host/task_manager_window.{h,cpp}`, widget merged into
`cerf/host/host_auto_resize.{h,cpp}`, pinned in the status bar's terminal
range), and the guest-side pump `ce_apps/cerf_guest/cerf_task_manager_pump.cpp`.

Channel-design rules this subsystem proved - they bind every future `cerf_virt`
channel:

- **No user API in a guest pump before full boot.** Pump start is display-driver
  init, mid-boot; a user API there (LoadLibrary included) corrupts gwes boot.
  Resolve dependencies lazily on first command - commands can only arrive
  post-boot.
- **Bulk guest→host data crosses through the MMIO regs page, never as a guest
  table VA the host reads.** FCSE kernels lazily zero L2 entries under
  TLB-resident pages, so a host-side walk fails for memory the guest reads fine;
  `PeekVaToHost` is reliable only for small just-written buffers (the gpe
  descriptors).
- **cerf_guest.dll's virtual image stays small - big buffers are allocated at
  first use, not declared as large statics.** The body is delivered over the
  `cerf_virt` body channel and manual-mapped (VirtualAlloc), so a large static
  array bloats the image past the channel's `kGuestBodyMaxSize` cap
  (`cerf_virt_addr_map.h`).
- **Kernel-loaded guest code (CE6+ drivers) cannot use callback-marshaled
  APIs** - gwes rejects caller-supplied function pointers from kernel space
  (EnumWindows); use handle-walk equivalents (GetWindow chain).
