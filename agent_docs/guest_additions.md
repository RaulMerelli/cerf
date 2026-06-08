# Guest Additions

Opt-in (`--guest-additions`, off by default). At ROM load CERF replaces the
board's stock display driver with the universal `cerf_guest` driver and brings
up a set of host-integration features on top of it: host-framebuffer rendering,
host-accelerated blitting, a mouse-pointer cursor, dynamic screen resolution,
shared host-folder storage, and a guest task manager. The subsystem spans host
C++ under `cerf/boot/` + `cerf/peripherals/cerf_virt/` and guest ARM code under
`ce_apps/cerf_guest/` + `ce_apps/cerf_guest_stub/`.

## How cerf_guest is injected (the universal mechanism)

`cerf_guest` is larger than a typical victim ROM slot, must survive boards that
wipe DRAM at boot, and must not be rejected by Windows Mobile module
authentication. One mechanism satisfies all three on every XIP/MultiXIP ROM,
every CE version:

- A **tiny stub** (`ce_apps/cerf_guest_stub/`) is injected as the victim
  display-driver module — the injector overwrites the victim's TOC entry in
  place to repoint it at the stub (`cerf/boot/guest_additions_injector.cpp`).
  Because the stub is tiny it fits any victim's slot on every CE version,
  erasing the per-version placement differences the full body would hit.
- The **full cerf_guest body** is delivered separately over a `cerf_virt` MMIO
  channel (`cerf/peripherals/cerf_virt/cerf_virt_guest_body.cpp`; the binaries
  are staged by `cerf/boot/guest_additions_binaries.{h,cpp}`). The stub pulls
  the body bytes and **manual-maps the PE itself** — VirtualAlloc + section copy
  + base relocation + import bind + entry — so the kernel's verified loader
  never sees the unsigned body. This is what lets it load past WM5 / WM2003SE
  module authentication and live in CERF-owned memory a firmware RAM wipe can't
  reach.
- **IMGFS ROMs (WM6+)** use a separate file-level path,
  `cerf/boot/imgfs_injector.cpp` + `cerf/boot/ce_imgfs_patcher.{h,cpp}`.
- Placement math (vbase relocation for an oversized or section-0 victim; the
  per-process DLL-RW reservation) lives in
  `cerf/boot/guest_module_placer.{h,cpp}`.

The body's manual-map shape is a load-bearing contract: a single import DLL
(coredll), HIGHLOW-only relocations, no TLS. Rebuilding `cerf_guest` so it gains
a second import DLL, MOV32 relocations, or a TLS directory silently breaks the
stub's mapper.

## Shared host storage (AFS FSD in device.exe)

A host folder is mounted into the guest as a filesystem, live-toggleable at
runtime, on every CE version. It registers directly on coredll's **AFS API-set
primitive** (`RegisterAFSName` + `CreateAPISet` + `RegisterAFS`) the way the
in-ROM `fatfs.dll` does — so it needs no `fsdmgr.dll` at runtime and works
uniformly CE3→CE7. The FSD MUST run in **device.exe** (the legitimate FSD host);
hosting it in gwes is illegal and corrupts cross-process loads. `cerf_guest`
reaches device.exe via a **driver-in-driver carrier**: it registers a `CDD_*`
stream driver and `ActivateDevice`s it, so device.exe loads the same module and
runs the FSD from `CDD_Init`. Guest side: `ce_apps/cerf_guest/cerf_fs_*.c` +
`cerf_driver_in_driver.cpp`. Host transport: the `ServerPB` peripheral
`cerf/peripherals/cerf_virt/cerf_virt_folder_share*` + `folder_share_*`.

## The cross-process writable-state invariant

The injected module is loaded by more than one process — gwes (display) and
device.exe (the FSD carrier). On a guest kernel that gives the module no
per-process RW home, the module's **writable statics are one physical instance
shared across those processes**, and a per-process `VirtualAlloc` address stored
in such a static is meaningless in the other process. Two rules follow, and both
are mandatory for any injected guest module:

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
instead — re-audit load-bearing helpers for vestigiality after any change to how
their inputs are produced, rather than assuming they are still required.

## The version / ROM-class axis

- **FCSE (CE ≤ 5)** — one shared page table; low VAs relocated per process by
  the cp13 FCSE PID. The shared-writable-statics hazard above is an FCSE
  property.
- **ASID (CE6/7)** — per-process page tables; the same injected module's
  writable data is per-process by construction.
- **ROM classes** — XIP single, MultiXIP, and IMGFS (WM6+). XIP / MultiXIP go
  through `guest_additions_injector.cpp`; IMGFS through `imgfs_injector.cpp`.

## Display driver + blit pipeline

The universal CERF display driver, injected into the guest ROM at load time by
`--guest-additions`. Built from real CE driver sources against the CE6 DDGPE/GPE
libraries; a compatibility shim under `ce_apps/cerf_guest/shim/` reshapes the
driver-interface data at the OS boundary so the single CE6-based driver runs
unmodified across CE3 → CE7 and Windows Mobile 5/6 (each OS sees its own
generation's shapes; the driver always sees CE6 shapes).

It is the guest-side partner of the host `cerf/peripherals/cerf_virt/` virtual
framebuffer + `gpe_cmd` accelerator channel: the driver routes blits over that
channel and the host performs them natively (see `cerf_ddgpe.cpp` for the
`BltPrepare` routing, `main.cpp` for the channel ABI). What it owns today is the
universal display path — host-framebuffer rendering plus host-accelerated blits.
Planned growth is host-side GPU acceleration, runtime screen resize, host↔guest
shared storage / clipboard, and a guest-additions input path.

**HW means HW.** The `cerf_guest` accelerator must handle every blit class
(copy, fill, format-convert, palette, masked text, transparency, ROP, …)
host-side through the virtual accelerator channel. Routing a blit class to the
guest's software render path (GPE `EmulatedBlt`) because the case is complex,
format-converting, masked, or transparent is forbidden and is a bailout — "let
it stay software" is never the fix. The software path is an extreme edge
reserved only for genuinely un-accelerable inputs (a guest page that cannot be
translated), never a design choice for a hard blit; a blit shape that renders
correctly under software but is declined to it is unaccelerated work, not a
finished feature.

## Task manager — host UI + cerf_virt channel + guest pump

The guest task manager (Guest Additions status-bar widget → non-modal host
window) lists, kills, and switches to guest processes and runs guest
executables. Three pieces: the MMIO command/response channel
`cerf/peripherals/cerf_virt/cerf_virt_task_manager.{h,cpp}` (+ `_regs.h`
contract — host publishes one command at a time via gen bump; guest answers with
response regs + kick; LIST rows stream guest→host one record at a time through a
register window), the host window + widget
(`cerf/host/task_manager_window.{h,cpp}`, widget merged into
`cerf/host/host_auto_resize.{h,cpp}`, pinned in the status bar's terminal
range), and the guest-side pump `ce_apps/cerf_guest/cerf_task_manager_pump.cpp`.

Channel-design rules this subsystem proved — they bind every future `cerf_virt`
channel:

- **No user API in a guest pump before full boot.** Pump start is display-driver
  init, mid-boot; a user API there (LoadLibrary included) corrupts gwes boot.
  Resolve dependencies lazily on first command — commands can only arrive
  post-boot.
- **Bulk guest→host data crosses through the MMIO regs page, never as a guest
  table VA the host reads.** FCSE kernels lazily zero L2 entries under
  TLB-resident pages, so a host-side walk fails for memory the guest reads fine;
  `PeekVaToHost` is reliable only for small just-written buffers (the gpe
  descriptors).
- **cerf_guest.dll's virtual image must fit the ROM victim slot it is injected
  into.** A large static array overflows the slot and the in-place injection
  corrupts the adjacent kernel-space module; big buffers are allocated at first
  use instead.
- **Kernel-loaded guest code (CE6+ drivers) cannot use callback-marshaled
  APIs** — gwes rejects caller-supplied function pointers from kernel space
  (EnumWindows); use handle-walk equivalents (GetWindow chain).
