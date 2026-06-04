# <img src="gweslab.png" width="24" height="24" /> **CE Runtime Foundation** v3.2 pre-alpha

A universal Windows CE emulator: a virtual ARM hardware platform that boots real CE and Windows Mobile ROMs on modern Windows.

> [!WARNING]
> **Early stage.** There are some bugs and boards are just MVP implementations. Some boards lack proper clocks, timings, caches, etc. - take into account. Today this is rather proof-of-concept. Contributions are welcome!

> [!TIP]
> Stock touch input is misbehaving in some devices/requires some additional effort. If your clicks do not register, try holding the left button and wiggling the cursor a bit. 

<p align="center">
  <img src="https://cerf.dz3n.net/promo1_02062026_1900.gif" alt="CERF — Windows CE virtual platform (part 1)" />
</p>
<p align="center">
  <img src="https://cerf.dz3n.net/promo2_02062026_1900.gif" alt="CERF — Windows CE virtual platform (part 2)" />
</p>

## Usage

The easiest way to run CERF is **`launcher.exe`** — a GUI app shipped next to `cerf.exe` that downloads publicly available ROM bundles and boots them. Pick a device from the list, tweak launch options (resolution, logging, network) if you want, click **Launch CERF**.

![launcher screenshot](docs/launcher.png)

For direct invocation without the launcher:

| Command                        | Action                                                       |
| ------------------------------ | ------------------------------------------------------------ |
| `cerf.exe `                    | Boot default device (ce5_smdk2410)                           |
| `cerf.exe --device=devemu_ce6` | Boot specific device                                         |
| `cerf.exe --log=ALL`           | Enable every log channel                                     |
| `cerf.exe --flush-outputs`     | Force-flush logs (avoid truncation on crash, extremely slow) |

Logs are written to `cerf.log` next to the executable. On a fatal crash, every other thread's register state and a top-of-stack snapshot is dumped to `cerf.crash.log` next to it. Run `cerf.exe --help` for the full CLI.

> [!NOTE]
> **`cerf.log` is quiet by default** — only critical `CERF` / `CAUTION` lines are written. Pass `--log=ALL` (or a channel list, e.g. `--log=BOOT,JIT,MMU`) to turn channels on.

## <img src="gweslab.png" width="24" height="24" /> Guest Additions

> [!WARNING]
> **Experimental and unstable.** Guest Additions are opt-in (`--guest-additions`), off by default. Expect per-device rendering glitches and reduced stability — some guest OSes behave better than others.

Guest Additions mechanism injects **pre-built ARM driver**, replacing the matching ROM's video driver. The library is fully OS version-agnostic and orchestrates entire set of features along with regular video driver responsibilities.

Pass `--guest-additions` (or tick the matching launcher option) to enable them.

### Features

- 32bpp custom screen resolution (boot CE3 into 4K!)
- Host-accelerated blitting - the driver routes blits to host which performs the full set of graphical operations in native code
- Dynamic screen resolution (CE 4+)
- Shared storage with host
- Mouse pointer driver:
  - required to avoid stock touch limitations on custom resolutions
  - guest OS cursor shape translated directly into host graphics
  - scroll wheel support on newer CE
  
> [!WARNING]
> **Touch breaks at non-native resolution.** The board's touch peripheral still uses the device's original input driver, which expects the original screen dimensions. With guest additions enabled, the main default input is the regular mouse cursor emulator that every (maybe) OS supports. In case if you need to go back to original touch interface, use the runtime switcher in Actions menu or in status bar. However it might be really corrupted on custom resolutions. E.g. iPaq H3600 devices seem to allow you to run calibration app only through stock stylus - the single app ignores the mouse pointer input.

## Supported boards

<table>
  <thead>
    <tr>
      <th>SoC</th>
      <th>Board / Device ID / OS</th>
      <th>Features</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td align="center"><b><img src="docs/icons/chip.png" width="16" height="16" title="Chip" alt="Chip"/> Intel SA-1110</b><br/><sub>StrongARM</sub></td>
      <td>
        <img src="docs/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>Compaq iPAQ H3600 Series</b><br/>
        <img src="docs/icons/os_ppc2000.png" width="16" height="16" title="Pocket PC 2000" alt="Pocket PC 2000"/> Pocket PC 2000 <code>ipaq_h3600_ppc2000</code><br/>
        <img src="docs/icons/os_ppc2002.png" width="16" height="16" title="PPC2002+ Icon" alt="PPC2002+ Icon"/> Pocket PC 2002 <code>ipaq_h3600_ppc2002</code>
      </td>
      <td><img src="docs/icons/display.png" width="16" height="16" title="Graphics" alt="Graphics"/> <img src="docs/icons/speaker.png" width="16" height="16" title="Sound" alt="Sound"/> <img src="docs/icons/stylus.png" width="16" height="16" title="Touch" alt="Touch"/></td>
    </tr>
    <tr>
      <td align="center"><b><img src="docs/icons/chip.png" width="16" height="16" title="Chip" alt="Chip"/> Intel XScale PXA255</b><br/><sub>ARMv5TE</sub></td>
      <td>
        <img src="docs/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>Datalogic Falcon 4220</b> (Askey PC3xx)<br/>
        <img src="docs/icons/os_ce.png" width="16" height="16" title="Windows CE" alt="Windows CE"/> Windows CE .NET 4.2 <code>falcon_4220__4_10</code>
      </td>
      <td><img src="docs/icons/display.png" width="16" height="16" title="Graphics" alt="Graphics"/> <img src="docs/icons/speaker.png" width="16" height="16" title="Sound" alt="Sound"/> <img src="docs/icons/stylus.png" width="16" height="16" title="Touch" alt="Touch"/></td>
    </tr>
    <tr>
      <td align="center"><b><img src="docs/icons/chip.png" width="16" height="16" title="Chip" alt="Chip"/> Microsoft ODO (???)</b><br/><sub>ARM720T (1996 NDA board)</sub></td>
      <td>
        <img src="docs/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>ODO/Poseidon</b> (???)<br/>
        <img src="docs/icons/os_old_ce.png" width="16" height="16" title="Windows CE (Classic)" alt="Windows CE (Classic)"/> Windows CE 2.11 <code>odo_poseidon_ce2</code><br/>
        <img src="docs/icons/os_old_ce.png" width="16" height="16" title="Windows CE (Classic)" alt="Windows CE (Classic)"/> Windows CE 3 <code>odo_poseidon_ce3</code>
      </td>
      <td><img src="docs/icons/display.png" width="16" height="16" title="Graphics" alt="Graphics"/> <img src="docs/icons/speaker.png" width="16" height="16" title="Sound" alt="Sound"/> <img src="docs/icons/stylus.png" width="16" height="16" title="Touch" alt="Touch"/> <img src="docs/icons/keyboard.png" width="16" height="16" title="Keyboard" alt="Keyboard"/></td>
    </tr>
    <tr>
      <td align="center"><b><img src="docs/icons/chip.png" width="16" height="16" title="Chip" alt="Chip"/> TI OMAP 3530</b><br/><sub>Cortex-A8</sub></td>
      <td>
        <img src="docs/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>OMAP 3530 EVM</b><br/>
        <img src="docs/icons/os_ce.png" width="16" height="16" title="Windows CE" alt="Windows CE"/> Windows CE 7 <code>omap_3530_evm_ce7</code>
      </td>
      <td><img src="docs/icons/display.png" width="16" height="16" title="Graphics" alt="Graphics"/> <img src="docs/icons/stylus.png" width="16" height="16" title="Touch" alt="Touch"/></td>
    </tr>
    <tr>
      <td align="center"><b><img src="docs/icons/chip.png" width="16" height="16" title="Chip" alt="Chip"/> Freescale i.MX31L</b><br/><sub>ARM1136</sub></td>
      <td>
        <img src="docs/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>Zune 30</b> (Pyxis Keel)<br/>
        <img src="docs/icons/os_zune.png" width="16" height="16" title="Zune OS" alt="Zune OS"/> Windows CE 5 <code>zune_keel</code>
      </td>
      <td><img src="docs/icons/display.png" width="16" height="16" title="Graphics" alt="Graphics"/> <img src="docs/icons/keyboard.png" width="16" height="16" title="Keyboard" alt="Keyboard"/></td>
    </tr>
    <tr>
      <td rowspan="3" align="center"><b><img src="docs/icons/chip.png" width="16" height="16" title="Chip" alt="Chip"/> Samsung S3C2410</b><br/><sub>ARM920T</sub></td>
      <td>
        <img src="docs/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>Device Emulator</b><br/>
        <img src="docs/icons/os_ce.png" width="16" height="16" title="Windows CE" alt="Windows CE"/> Windows CE 6 <code>devemu_ce6</code><br/>
        <img src="docs/icons/os_ppc2002.png" width="16" height="16" title="PPC2002+ Icon" alt="PPC2002+ Icon"/> Windows Mobile 5 <code>devemu_wm5</code><br/>
        <img src="docs/icons/os_wm6.png" width="16" height="16" title="Windows Mobile 6+" alt="Windows Mobile 6+"/> Windows Mobile 6 <code>devemu_wm6</code><br/>
        <p>...any many other WM5+/smartphone</p>
      </td>
      <td><img src="docs/icons/display.png" width="16" height="16" title="Graphics" alt="Graphics"/> <img src="docs/icons/speaker.png" width="16" height="16" title="Sound" alt="Sound"/> <img src="docs/icons/stylus.png" width="16" height="16" title="Touch" alt="Touch"/> <img src="docs/icons/keyboard.png" width="16" height="16" title="Keyboard" alt="Keyboard"/> <img src="docs/icons/internet.png" width="16" height="16" title="Network Emulation" alt="Network Emulation"/></td>
    </tr>
    <tr>
      <td>
        <img src="docs/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>Device Emulator (CE 4.2/5 branches)</b><br/>
        <img src="docs/icons/os_ppc2002.png" width="16" height="16" title="PPC2002+ Icon" alt="PPC2002+ Icon"/> WM 2003 SE <code>devemu_wm2003se</code><br/>
        <img src="docs/icons/os_ce.png" width="16" height="16" title="Windows CE" alt="Windows CE"/> Windows CE 5 <code>devemu_ce5</code><br/>
      </td>
      <td><img src="docs/icons/display.png" width="16" height="16" title="Graphics" alt="Graphics"/> <img src="docs/icons/speaker.png" width="16" height="16" title="Sound" alt="Sound"/> <img src="docs/icons/stylus.png" width="16" height="16" title="Touch" alt="Touch"/> <img src="docs/icons/keyboard.png" width="16" height="16" title="Keyboard" alt="Keyboard"/></td>
    </tr>
    <tr>
      <td>
        <img src="docs/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>PB SMDK 2410 Sample</b><br/>
        <img src="docs/icons/os_ce.png" width="16" height="16" title="Windows CE" alt="Windows CE"/> Windows CE 5 <code>smdk2410_sample_ce5</code>
      </td>
      <td>&mdash;</td>
    </tr>
  </tbody>
</table>

## How CERF runs ROM images? (NK.BIN, etc.)

Each device under `devices/<name>/` contains a Windows CE ROM image (`*.nb0` or `*.bin`) and each device declares an optional `cerf.json` describing itself and (optionally) overriding board / network / rom defaults:

```json
{
  "meta": {
    "device_name": "Microsoft Device Emulator (Windows Mobile 5 Pocket PC)",
    "board_name": "Device Emulator",
    "soc_family": "Samsung S3C2410 (ARM920T)",
    "os": { "name": "Windows Mobile", "ver_major": 5, "ver_minor": 0 },
    "device_year": 2005
  },
  "board": {
    "configurable_screen_width": 800,
    "configurable_screen_height": 600
  },
  "rom": {
    "primary": "NK.bin",
    "extensions": "EXT.bin",
    "recovery": "Recovery.bin"
  }
}
```

`meta` is informational (device identification for the launcher / status displays). `board` is only honoured by BSPs with a configurable screen resolution (today only Device Emulator boards). `rom` is only needed when a device ships more than one partition; single-ROM devices auto-detect the `*.nb0` / `*.bin`.

See [device_config.h](cerf/core/device_config.h) for the full schema.

To determine what is the board, CERF looks inside of ROM and performs heuristic search by module names or binary blobs. CERF also replaces entire bootloader, therefore e.g. Zune 30 can boot OS without HDD (tho OS actually will hang without HDD), but in reality it seems that the bootloader spins the HDD and boots NK.BIN from HDD. Our synthed Zune 30 HDD lacks NK.BIN entirely.

## Building

Requires Visual Studio 2026 with the C++ desktop development workload.

> [!NOTE]
> **First build on a fresh machine takes 1+ hour.** vcpkg compiles dependencies from source before CERF starts linking. This happens once per machine — subsequent builds reuse the cached `vcpkg_installed/` tree and finish in a few minutes. Do not interrupt the first build.

Initialise source/dependency submodules:

```
git submodule update --init --recursive
```

Build via the helper script:

```
powershell -ExecutionPolicy Bypass -File build.ps1
```

Or invoke msbuild directly:

```
msbuild cerf.sln /p:Configuration=Release /p:Platform=Win32
```

## Third-party / Credits

- **[QEMU](https://www.qemu.org/)** 
- **[The Linux kernel](https://www.kernel.org/)** 
- **[nlohmann-json](https://github.com/nlohmann/json)**
- **[libslirp](https://gitlab.freedesktop.org/slirp/libslirp)** 
- JIT studied/inspired by Microsoft's Device Emulator (Shared Source Academic License, 2006)

## Known Issues

- iPAQ H36xx series - hang after user interaction with device stopped for ~10 seconds (clock bugged?)
- iPAQ H36xx series - PCMCIA errors (not critical) - perhaps bad emulation/stubs
- DevEmu CE3 - PCMCIA errors
- OMAP 3530 EVM - XAML keyboard and IE are not rendering (blank white) and causing emulator to drop performance - do not open them
- Guest additions seem to destroy multi-XIP ROMs
- libslirp internet is extremely slow or doesn't work at all (CERF v1 bug)
- DevEmu WM2003SE - touch rarely works - reproducible on Device Emulator itself
- Keyboard seems to be broken on DevEmu WM ROMs
- Sound crippled on ODO and DevEmu - audio peripheral/ostimer problems

## Changelog

<table>
  <thead>
    <tr>
      <th>CERF Version</th>
      <th>Changes</th>
    </tr>
  </thead>
  <tbody>
    <tr>
          <td>v3.11 (For Workgroups)</td>
          <td>
            <ul>
              <li>Guest Additions: various video driver improvements/fixes</li>
              <li>Guest Additions: accelerated mouse pointer (configurable at runtime; scroll wheel on newer OS)</li>
              <li>Guest Additions: shared folders with host</li>
              <li>Guest Additions: auto screen resolution change on host window resize</li>
              <li>Falcon 4220 board partial implementation (OS boots to UI but hangs)</li>
              <li>Various bug fixes and improvements</li>
            </ul>
          </td>
        </tr>
    <tr>
          <td>v3.0</td>
          <td>
            <ul>
              <li>GUEST ADDITIONS - injects own video driver into ROMs</li>
              <li>OMAP 3530 EVM board support (MVP)</li>
              <li>Zune 30 board support (MVP)</li>
              <li>Massive emulator UI overhaul</li>
              <li>ARMv5,v6,v7 VFP/NEON MVP support, massive JIT improvements</li>
              <li>Tons of bug fixes and improvements</li>
            </ul>
          </td>
        </tr>
    <tr>
          <td>v2.0</td>
          <td>
            <ul>
              <li>Initial release</li>
              <li>ARMv4 support</li>
              <li>DevEmu, iPAQ H3600, Microsoft ODO support (MVP)</li>
            </ul>
          </td>
        </tr>
  </tbody>
</table>

## What happened to CERF v1?

> [!NOTE]
> CERF v1 reimplemented CE userspace + kernel in host C++ - coredll exports thunked, rehosted on Win32. It hit a hard ceiling: per-process host resources (GDI handles, atom tables, kernel handles) couldn't hold an entire guest OS. v1 was overengineering hell that literally grew exponentially. v2 is a completely different project. v1's source lives at [cerf-v1-obsolete](https://github.com/gweslab/cerf-v1-obsolete).

## AI-generated code

> [!CAUTION]
> **DO NOT USE CERF CODEBASE AS REFERENCE FOR SoCs, BOARDS, PERIPHERALS** - AI WRITTEN CODE CAN'T BE TRUSTED!

100% generated by [Claude](https://claude.ai) via [Claude Code](https://docs.anthropic.com/en/docs/claude-code) — no human-written code. Not production-grade.

## Downloads

[![build](https://github.com/gweslab/cerf/actions/workflows/build.yml/badge.svg)](https://github.com/gweslab/cerf/actions/workflows/build.yml)
