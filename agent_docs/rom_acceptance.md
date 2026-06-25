# ROM Acceptance - what CERF eats and how

CERF runs the original device's software unchanged. The CE binaries - kernel,
OAL, drivers, userspace - are the ROM's own guest code (see `agent_docs/rules.md`
§ WinCE Accuracy). This page is about the layer below that: what *file* CERF
accepts as "the ROM", and how it turns that file into a running guest.

The governing rule: **CERF accepts the original OEM firmware package / dump as
shipped, and extracts or serves the bootable image from it.** It does NOT accept
- and must never be made to require - a pre-extracted, repackaged, or otherwise
modified image produced by an external tool. If a device's bootable image lives
inside an OEM container or a whole-flash dump, CERF parses that container /
serves that flash; it does not ask the user (or a build step, or a remote
manifest) to hand it a stripped payload. A device that only boots from a
hand-extracted XIP is a CERF gap to close, not the intended shape.

This sits next to two related pages: `agent_docs/boot_loaders.md` (whether CERF
skips, models, or fully emulates the OEM bootloader) and the Guest-Additions
injection mechanism (`agent_docs/guest_additions.md`), which recomposes a ROM by
the same ROMIMAGE rules CERF parses it with.

## Two independent acceptance axes

Every supported device is classified on two axes. They are orthogonal - a device
picks one value on each.

### Axis 1 - original container vs raw payload

The OEM ships the bootable image inside a container with a header, sometimes a
signature catalog, sometimes multiple partitions. CERF recognizes the container
by its magic bytes and unwraps it. Recognized containers today:

- **flat NB0 / raw XIP** - no container; the file *is* the XIP. The common case.
- **B000FF** (`kB000FFSignature`, `"B000FF\n"`) - a sectioned image: a list of
  `(base, size, checksum) + data` sections, terminated by a `base==0` section
  whose `size` field carries the kernel entry VA. Assembled into a flat span by
  `AssembleB000FFFlat`. Pocket PC / multi-XIP NB0 dumps.
- **NOSAJ** (`kNosajSignature`, `"NOSAJ\0"`) - the SmartBook G138 `.fim`
  package: inline partition descriptors + a byte-reversed `"DiAlOgUe"` launch
  block that frames the OS XIP. Resolved by `NosajLocateOsXip`.
- **ARNOLDBOOTBLOCK** (`kArnoldSignature`) - the Siemens SIMpad (`"Arnold"`
  codename) `.bin` firmware package: a fixed header followed by the bootable OS
  XIP, byte-identical to what an extracted `.nb0` carries. Resolved by
  `ArnoldLocateOsXip`.

All of these are unwrapped to a **flat XIP span** that the rest of the pipeline
treats identically.

### Axis 2 - flat XIP vs whole storage

- **Flat XIP** - the bootable image is one contiguous XIP (kernel + ROM
  modules + ROM files), small enough to place in DRAM and execute. Handled by
  `RomParserService` (`cerf/boot/rom_parser_service.cpp`). The vast majority of
  boards.
- **Whole storage** - the dump is an entire flash / NAND / disk (often multiple
  GB), and the bootable image is a region *inside* it that the device's own boot
  path locates and copies to DRAM. CERF maps the dump on demand and serves it
  through the emulated storage controller; the guest reads it the way real
  hardware does. Ford SYNC 2 (`.sec` → `SecFlash` → i.MX51 NFC) is the current
  example.

## The flat-XIP pipeline (`RomParserService`)

`RomParserService::ParseOne` (`cerf/boot/rom_parser_service.cpp`) is the funnel.
For each declared / auto-detected ROM file:

1. **Read the whole file** into `rom.raw`.
2. **Detect the container** by leading magic, in order: B000FF → NOSAJ →
   ARNOLD → else flat NB0. The chosen branch sets `rom.flat` (a span over the
   bootable XIP) and `rom.flat_base_va` (the VA that file-offset 0 of the flat
   maps to).
3. **Find the ROMHDR.** Scan the flat for every `ECEC` ROM signature
   (`FindAllEcec`; `0x43454345` at XIP+0x40, with the pTOC kernel-VA at
   +0x44). Each marker is resolved to a ROMHDR by
   `ResolveRomhdrAtEcec`. A CE2-era image with no ECEC record falls back to
   `ResolveRomhdrStructural`, which scans for a self-consistent ROMHDR validated
   against an `nk.exe` module name.
4. **Parse the TOC** (`ParseModulesAndFiles`): `nummods` TOCentry records +
   `numfiles` FILESentry records, resolving names via `load_offset`.
5. **IMGFS** (WM6+ only): `FindImgfsBase` locates the IMGFS superblock; the
   walker (`ce_imgfs_walker`) enumerates flash-filesystem modules. Skipped for
   container formats that are pure XIP (NOSAJ, ARNOLD) and for B000FF.

The outputs every downstream consumer relies on are `rom.flat`,
`rom.flat_base_va`, `rom.entry_va`, and `rom.xips[*].toc`. A new container format
only has to populate `rom.flat` / `rom.flat_base_va` correctly; steps 3-5 are
shared.

### Adding a container format

Mirror the existing ones - they share a shape:

- A signature constant + a `*LocateOsXip` resolver in
  `cerf/boot/rom_image_parse.{h,cpp}` that returns the XIP's `data_off`,
  `flat_size`, and `base_va`. The base VA, when not stored in the container, is
  recovered by validating the candidate ROMHDR against the container's own
  invariants (NOSAJ checks `physlast - physfirst == span`; ARNOLD checks
  `physfirst == candidate base`). This is ROM-content validation, the same kind
  `BoardDetector` and the ECEC resolver already do - not a JIT/MMU heuristic.
- A detection branch + an `is_<format>` flag in `ParseOne` / `ParsedRom` that
  sets `rom.flat` and `rom.flat_base_va`, then lets the shared ECEC/ROMHDR/TOC
  path run.

Everything that locates the XIP, the base, or the entry must come from the ROM's
own bytes - never `cerf.json`, `meta`, a whole-image CRC, or runtime RAM
heuristics (`agent_docs/rules.md` § "Per-device facts come from the ROM").

## The whole-storage pipeline (`SecFlash` + storage controller)

For multi-GB dumps the bootable image is not extracted to a flat span - the
guest's own storage stack reads it. Ford SYNC 2:

- **`SecFlash`** (`cerf/boot/sec_flash.{h,cpp}`) owns the `.sec` NAND image as a
  `MappedFile` (the ~2 GiB file is never mapped whole) and a parsed
  `SecContainer` (`cerf/boot/sec_container.{h,cpp}`) that de-chunks the package
  (chunked payload + PKCS#7 catalog; `SecHeader` magic `0x400D400D`). It
  registers only when the device dir holds a `.sec`; consumers `TryGet` and
  tolerate absence.
- **The i.MX51 NAND path** consumes it: `imx51_nand_bootloader_boot.cpp` models
  the NAND boot ROM (reads a flash header from `SecFlash`, copies the bootable
  image to DRAM), and `imx51_nfc.cpp` (the NAND Flash Controller) serves NAND
  pages from `SecFlash` on demand so the guest's flash filesystem reads them like
  real silicon.

`RomParserService` is not involved - `.sec` is not in its file-extension list,
and Sync 2 boots entirely through emulated NAND. This is the same family as the
Zune approach in `agent_docs/boot_loaders.md`, where CERF synthesizes a blank
disk with the expected partition and the OS boots from it: the bootable image
reaches DRAM through emulated storage + the device's own boot path, not through a
host-side XIP extractor.

Choosing this pipeline is a property of the dump, not a convenience: a whole-NAND
container that wraps a flash filesystem (BINFS / IMGFS over an FTL) has no single
flat XIP to lift out, so the controller must be emulated faithfully and the guest
left to read its own storage.

## Where the file comes from

`RomParserService::OnReady` resolves the file set: an explicit `rom_primary`
(+ `rom_extensions`, or `rom_recovery` under `--recovery`) from `DeviceConfig`,
else the first `*.nb0` / `*.bin` / `*.fim` found in the device directory.
Selecting which file boots when a bundle ships several - a multi-ROM-file
bundle such as a ROM plus a separate EEPROM image, which ROM content alone
cannot disambiguate - is the one operational job `cerf.json`'s `rom` block
legitimately does. Device facts (board, SoC, version, memory map) and the
XIP / base / entry resolution above still come from the ROM's own bytes, never
`cerf.json` or `meta` (`agent_docs/rules.md`). The bundled device tree
(`bundled/devices/<name>/`) holds the original OEM file - the package or dump as
shipped, not a derived artifact.
