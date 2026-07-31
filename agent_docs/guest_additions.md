# Guest Additions

Opt-in (`--guest-additions`, off by default). At ROM load CERF replaces the
stock display driver of the board with the universal `cerf_guest` driver. CERF
then starts a set of host-integration features on top of that driver:
host-framebuffer rendering, host-accelerated blitting, a mouse-pointer cursor,
keyboard injection, dynamic screen resolution, shared host-folder storage, and
a guest task manager. The subsystem spans host C++ under `cerf/boot/` +
`cerf/peripherals/cerf_virt/` and guest CE code under `ce_apps/cerf_guest/` +
`ce_apps/cerf_guest_stub/`.

## How cerf_guest is injected (the universal mechanism)

`cerf_guest` is larger than a typical victim ROM slot. It must survive boards
that wipe DRAM at boot. It must also pass Windows Mobile module
authentication. One mechanism satisfies all three on every ROM class and every
CE version. Two pieces are common to all ROMs:

- CERF injects a **tiny stub** (`ce_apps/cerf_guest_stub/`) as the victim
  display-driver module. The injector repoints the module record of the victim
  at the stub. The bytes of the stub live in dedicated memory, never squatted
  into the image of the victim. A tiny stub stays under the per-CE-version
  slot-size limits that the full body exceeds.
- A `cerf_virt` MMIO channel
  (`cerf/peripherals/cerf_virt/cerf_virt_guest_body.cpp`) delivers the **full
  cerf_guest body** separately. The stub and body are staged per guest
  architecture under `ce_apps/<arch>/`. `GuestAdditionsBinaries::ArchDir`
  (`cerf/boot/guest_additions_binaries.{h,cpp}`) selects them from
  `GetCpuArch()` plus the CPU subtype of the ROM. The stub reads the body bytes
  and **manual-maps the PE itself**: VirtualAlloc + section copy + base reloc +
  coredll import bind + entry. The verified loader of the kernel therefore never
  sees the unsigned body. This mechanism loads past WM5 / WM2003SE module
  authentication. It gives each process that loads the module its OWN body
  image (per-process by construction). It also lives in CERF-owned memory that
  a firmware RAM wipe cannot reach.

Only **the placement of the stub into the ROM** differs by ROM class:

- **XIP / MultiXIP** (`cerf/boot/guest_additions_injector.cpp`) - the ROM
  carries a TOC of XIP modules. The injector therefore overwrites the TOC entry
  of the victim module (its e32/o32/load offsets) to repoint it at the stub.
  The e32/o32 records and section bytes of the stub live in a CERF-owned PA
  band in the `cerf_virt` window (`cerf/boot/cerf_injection_region.{h,cpp}`).
  That band is exposed at a guest-unmapped static-window VA, a hole that
  `PageTableBuilder::StaticWindowHole` finds over the `MappedVaSpans()` of the
  board. An MMU-walker overlay serves the band (`ArmMmu::ServeInjectionBand` at
  the L1-fault site), never the section bytes of the victim. On CE6/7 the stub
  runs in place from the band, because a kernel-VA base makes the loader skip
  its section copy. On CE3/4/5 the loader copies the stub to a section-1 vbase
  (`GuestModulePlacer::ComputeVbase`). The band sections are flagged
  MEM_WRITE|SHARED, so the loader takes the overlay-servable memcpy with no
  per-process slot-base fold on the device.exe carrier load.
- **IMGFS** (WM6+, `cerf/boot/imgfs_injector.cpp` + `ce_imgfs_patcher.{h,cpp}`)
  - IMGFS is a flash filesystem (an FTL over the NOR/NAND image), not an XIP
  TOC. There is therefore no slot to overwrite. The injector allocates fresh
  FTL flash pages and writes the logical-sector-map entry of each page, so
  imgfs.dll adopts it. It writes the `e32_rom` module header and per-section
  data of the stub into those pages. It then repoints the IMGFS **dirent**
  module-index / section-index records of the victim at the new pages. CERF
  injects the SAME stub. Only the placement substrate is the FTL instead of
  the TOC.

Every path flags the writable sections of the stub SHARED, through
`GuestModulePlacer::EffSectionFlags` or directly next to MEM_WRITE on the
CE3/4/5 XIP copy path. The stub also keeps its per-process state pid-keyed.
See § The cross-process writable-state invariant for the reason.

The manual-map shape of the body is a load-bearing contract: a single import
DLL (coredll), HIGHLOW-only relocations, no TLS. A rebuild of `cerf_guest` that
gains a second import DLL, MOV32 relocations, or a TLS directory silently
breaks the mapper of the stub.

The body imports coredll **by name**, not by ordinal. High ordinals are
per-CE-version with no cross-version contract, and the names are the contract.
The stub resolves each name with `GetProcAddress` in the process that loads it.
The trap: a coredll function that some target coredll does not export **by
name** cannot be resolved. Such a function is exported by ordinal only, or it
is absent entirely. The by-name manual-map then fails, and the whole body never
loads. The coredll of Pocket PC 2000 exports `CeSetExtendedPdata` ordinal-only.
The CE2.0 coredll lacks the CRT and the 64-bit compiler helpers entirely. Older
coredlls are where this bites.

Such a symbol must be **defined locally in cerf_guest** instead of imported. A
local definition wins over the import-lib thunk, so the build emits no coredll
import for it. `cerf_ce2_crt.cpp` does this for the CE2.0 target: `memset` /
`memcpy` / `operator new` / `operator delete` and the compiler helpers
`__ll_lshift` / `__ll_div`. That coredll exports none of them. Before you add a
coredll import to `cerf_guest`, verify that the **oldest** target coredll
exports it by name. If it does not, define it locally.

## Shared host storage (AFS FSD in device.exe)

CERF mounts a host folder into the guest as a filesystem, on every CE version.
The user toggles the mount live at runtime. The FSD registers directly on the
**AFS API-set primitive** of coredll (`RegisterAFSName` + `CreateAPISet` +
`RegisterAFS`), the way the in-ROM `fatfs.dll` does. It therefore needs no
`fsdmgr.dll` at runtime and works uniformly CE3→CE7.

The FSD MUST run in **device.exe** (the legitimate FSD host). An FSD hosted in
gwes is illegal and corrupts cross-process loads. `cerf_guest` reaches
device.exe through a **driver-in-driver carrier**: it registers a `CDD_*`
stream driver and calls `ActivateDevice` on it, so device.exe loads the same
module and runs the FSD from `CDD_Init`. Guest side:
`ce_apps/cerf_guest/cerf_fs_*.c` + `cerf_driver_in_driver.cpp`. Host transport:
the `ServerPB` peripheral
`cerf/peripherals/cerf_virt/cerf_virt_folder_share*` + `folder_share_*`.

## The cross-process writable-state invariant

More than one process loads the injected module: gwes (display), device.exe
(the FSD carrier), and any DirectDraw-HAL client that loads the display driver
(`HALInit`). The vbase of the module decides whether the kernel gives each
process its own writable copy. **A CE5/FCSE kernel folds a `WRITE & !SHARED`
section to a per-process slot copy only when the vbase is in a slot / section-1
region. A module at a high shared-region vbase keeps one physical `.data`
shared across every process that loads it.** For this reason a low-vbase IMGFS
victim (WM6.0) tolerated a full kernel-loaded body, and a high-vbase one
(WM6.5) corrupted the globals of gwes cross-process. This is also the reason
the per-process manual-mapped stub body exists.

Where the writable statics ARE shared, a per-process `VirtualAlloc` address
stored in such a static is meaningless in the other process. Two rules follow.
Both are mandatory for any injected guest module:

- **Per-process runtime state must be keyed by process id.** One process
  initializes a flat static. The next process reads the value of the first
  process and then skips its own setup. (The debug-log channel and the
  mapped-body + resolved-export table of the stub are both pid-keyed for this
  reason.)
- **Writable injected sections are flagged SHARED.** The CE loader gives a
  per-process copy only to `WRITE & !SHARED` sections. With no per-process
  DLL-RW reservation, that copy folds to the bare slot base and faults the
  second loader. The SHARED flag keeps the section as one shared copy, and
  pid-keyed runtime state then makes it correct. This is one mechanism that
  works on FCSE and ASID kernels alike, with no per-board reservation.

Corollary: a placement/reservation mechanism that was correct for a
kernel-loaded module becomes vestigial once manual-map delivers the body.
After any change to how the inputs of a load-bearing helper are produced,
re-audit that helper for vestigiality. Never assume that it is still required.

## The version / ROM-class axis

- **FCSE (CE ≤ 5)** - one shared page table. The cp13 FCSE PID relocates low
  VAs per process. The shared-writable-statics hazard of the previous section
  is an FCSE property.
- **ASID (CE6/7)** - per-process page tables. The writable data of the same
  injected module is per-process by construction.
- **ROM classes** - XIP single, MultiXIP, and IMGFS (WM6+). XIP / MultiXIP go
  through `guest_additions_injector.cpp`. IMGFS goes through
  `imgfs_injector.cpp`.

## Display driver + blit pipeline

The universal CERF display driver. `--guest-additions` injects it into the
guest ROM at load time. The body is the GPE/DDGPE/DDI scaffolding of CERF
(`ce_apps/cerf_guest/include/cerf_gpe.h`, `include/cerf_ddi.h`) plus the driver
built on it. It links `coredll` only. A per-version compatibility layer
reshapes the driver-interface data at the OS boundary. That layer is the
DDI/DDHAL translation (`cerf_ddi_*.cpp`, `cerf_ddhal*.cpp`) and the
per-generation ddraw headers
(`include/ddraw_ce5.h` / `ddraw_ce6.h` / `ddraw_wm.h`). The single CE6-shaped
driver therefore runs unmodified across CE2.0 → CE7 and Windows Mobile 5/6.
Each OS sees the shapes of its own generation. The driver always sees CE6
shapes.

It is the guest-side partner of the host `cerf/peripherals/cerf_virt/` virtual
framebuffer and `gpe_cmd` accelerator channel. The driver routes blits over
that channel, and the host performs them natively (`cerf_ddgpe_blt.cpp`
`BltPrepare` routing, `main.cpp` channel ABI). It owns the universal display
path: host-framebuffer rendering, host-accelerated blits, a DirectDraw HAL,
dynamic resolution, and the cursor / keyboard / shared-storage / task-manager
transport.

**HW means HW.** The `cerf_guest` accelerator must handle every blit class
(copy, fill, format-convert, palette, masked text, transparency, ROP, …)
host-side through the virtual accelerator channel. A complex, format-converting,
masked, or transparent case is never a reason to route a blit class to the
software render path of the guest (GPE `EmulatedBlt`). Such a route is forbidden
and is a bailout. "Let it stay software" is never the fix. The software path is an
extreme edge, reserved only for genuinely un-accelerable inputs (a guest page
that CERF cannot translate). It is never a design choice for a hard blit. A
blit shape that renders correctly under software, but that you declined to that
path, is unaccelerated work, not a finished feature.

**Display color model.** Direct-color on CE3+/CE2.11 (≥16bpp). But **CE2.0
must be 8bpp-indexed**: its gwes creates only `PAL_INDEXED`. A ≥16bpp
framebuffer therefore leaves `hpalDefault` = 0 and faults gwes (Exception 002)
in display init. A CE2.0 board forces 8bpp with
`BoardContext::GetGuestAdditionsColorDepth()` = 8. The indexed path publishes
the palette over the `cerf_virt` palette channel, and the host expands
index→RGB on scanout. The framebuffer bpp selects the path, never the OS
version (CE2.11 is already ≥16bpp).

**The floor is CE 2.0. CE 1.0 GA is impossible, not unimplemented.** GA
replaces the *loadable* display driver of the guest, the DDI `DrvEnableDriver`
that the body implements. That DDI is present from CE 2.0. CE 1.0 gwes is the
whole graphics stack. It renders every pixel into the physical LCD aperture
itself (GDI is an in-gwes apiset). It exports no `DrvEnableDriver`. Its only
external display hook is an optional dirty-rectangle flush
(`DEVICEMAP\DISPLAY\DriverName` → `DirtyRectInit`/`DirtyRectUpdate`), not a
renderer. With no replaceable driver, the core of GA cannot attach. The CE 1.0
desktop reaches the host through the native LCD aperture of the board. Do not
attempt CE 1.0 GA.

## Keyboard injection

Host keystrokes reach the guest through the same mechanism as the mouse-pointer
pump, with `keybd_event` in place of `mouse_event`. Both are coredll exports
that trap into the same GWES API set (API-set 81). Both are present unchanged
on every CE version (CE 2.11 → CE 7). One guest binary therefore drives
keyboard input on every guest OS, with no per-version code and no
guest-installed driver.

- Host channel: `cerf/peripherals/cerf_virt/cerf_virt_keyboard.{h,cpp}` +
  `cerf_virt_keyboard_regs.h`. Keys are edges (down/up) that must arrive once
  and in order. The channel is therefore a ring buffer, not the level /
  last-value-wins registers of the pointer. The host appends one entry per
  host-key transition and increments the write sequence after the entry store.
  The guest drains entries by index.
- Guest pump: `ce_apps/cerf_guest/cerf_keyboard_pump.cpp`. It is a thread that
  `DrvEnablePDEV` of the display driver starts, next to the pointer / resize /
  task pumps. It resolves `keybd_event` through `GetProcAddressW` and replays
  each ring entry. GWES routes each entry to the focused window. GWES then runs
  the active keyboard layout to produce WM_KEYDOWN / WM_KEYUP + WM_CHAR.

The guest-additions keyboard registers as one source in the host
`KeyboardRouter` (`cerf/host/keyboard_router.{h,cpp}`) with the highest source
priority. It is therefore the active source whenever guest additions are on.
The router is board-agnostic host input and is not itself part of guest
additions - see `subsystems.md`.

The guest-additions mouse registers the same way: one source in the host
`PointerRouter` (`cerf/host/pointer_router.{h,cpp}`) at the highest source
priority. The GA absolute pointer is therefore the active device whenever guest
additions are on. The `PointerWidget` switches it for the stock pointer or
pointers of the board. That switch is necessary because some apps (for example
calibrators) read the stock touch/mouse stream directly. On a board whose stock
pointer is a relative mouse, the host-capture mouse lock engages only while
that source is active. Router and widget are board-agnostic host input, not
part of guest additions - see `subsystems.md`.

## Task manager - host UI + cerf_virt channel + guest pump

The guest task manager (Guest Additions status-bar widget → non-modal host
window) lists, kills, and switches to guest processes. It also runs guest
executables. It has three pieces:

- the MMIO command/response channel
  `cerf/peripherals/cerf_virt/cerf_virt_task_manager.{h,cpp}` plus the
  `_regs.h` contract. The host publishes one command at a time through a gen
  bump. The guest answers with response regs plus a kick. LIST rows stream
  guest→host one record at a time through a register window.
- the host window and widget (`cerf/host/task_manager_window.{h,cpp}`, widget
  merged into `cerf/host/host_auto_resize.{h,cpp}`, pinned in the terminal
  range of the status bar).
- the guest-side pump `ce_apps/cerf_guest/cerf_task_manager_pump.cpp`.

Channel-design rules that bind every future `cerf_virt` channel:

- **No user API in a guest pump before full boot.** Pump start is
  display-driver init, mid-boot. A user API there (LoadLibrary included)
  corrupts gwes boot. Resolve dependencies lazily on the first command.
  Commands arrive only after boot.
- **Bulk guest→host data crosses through the MMIO regs page, never as a guest
  table VA that the host reads.** FCSE kernels lazily zero L2 entries under
  TLB-resident pages. A host-side walk therefore fails for memory that the
  guest reads without a problem. `PeekVaToHost` is reliable only for small
  just-written buffers (the gpe descriptors).
- **The virtual image of `cerf_guest.dll` stays small. Allocate big buffers at
  first use. Never declare them as large statics.** The `cerf_virt` body
  channel delivers the body, and the stub manual-maps it (VirtualAlloc). A
  large static array therefore bloats the image past the `kGuestBodyMaxSize`
  cap of the channel (`cerf_virt_addr_map.h`).
- **Kernel-loaded guest code (CE6+ drivers) cannot use callback-marshaled
  APIs.** gwes rejects caller-supplied function pointers from kernel space
  (EnumWindows). Use handle-walk equivalents (GetWindow chain).
