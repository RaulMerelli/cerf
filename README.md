# <img src="gweslab.png" width="24" height="24" /> **CE Runtime Foundation** v4.1 pre-alpha [![Discord](https://img.shields.io/badge/Discord-join%20the%20server-5865F2?logo=discord&logoColor=white)](https://discord.gg/QREE9Y2v2d)

A universal Windows CE emulator: a virtual ARM hardware platform that boots real CE and Windows Mobile ROMs on modern Windows.

<p align="center">
  <a href="https://www.youtube.com/watch?v=LmfaXUNGFlU">
    <img src="docs/cerf_youtube.png" alt="YouTube Preview" width="640" height="360" />
  </a>
</p>

> [!WARNING]
> **Early stage.** There are some bugs and boards are just MVP implementations. Some boards lack proper clocks, timings, caches, etc. - take into account. Today this is rather proof-of-concept. Contributions are welcome!

> [!TIP]
> Stock touch input is misbehaving in some devices/requires some additional effort. If your clicks do not register, try holding the left button and wiggling the cursor a bit.


## Usage

The easiest way to run CERF is **`launcher.exe`** — a GUI app shipped next to `cerf.exe` that downloads publicly available ROM bundles and boots them. Pick a device from the list, tweak launch options (resolution, logging, network) if you want, click **Launch CERF**.

![launcher screenshot](docs/launcher.png)

For direct invocation without the launcher:

| Command                        | Action                                                       |
| ------------------------------ | ------------------------------------------------------------ |
| `cerf.exe `                    | Boot default device (cerfos)                                 |
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
- Task manager in a host window
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
      <th>Board / OS</th>
      <th>Features</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td rowspan="2" align="center"><b><img src="launcher/assets/icons/chip.png" width="16" height="16" title="Chip" alt="Chip"/> Intel XScale PXA255</b><br/><sub>ARMv5TE</sub></td>
      <td>
        <img src="launcher/assets/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>Falcon 4220</b><br/>
        <img src="launcher/assets/icons/os_ce.png" width="16" height="16" title="Windows CE .NET" alt="Windows CE .NET"/> Windows CE .NET
      </td>
      <td><img src="launcher/assets/icons/display.png" width="16" height="16" title="Display" alt="Display"/> <img src="launcher/assets/icons/stylus.png" width="16" height="16" title="Touch" alt="Touch"/></td>
    </tr>
    <tr>
      <td>
        <img src="launcher/assets/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>NEC MobilePro 900</b><br/>
        <img src="launcher/assets/icons/os_old_ce.png" width="16" height="16" title="Handheld PC 2000" alt="Handheld PC 2000"/> Handheld PC 2000<br/>
        <img src="launcher/assets/icons/os_ce.png" width="16" height="16" title="Windows CE .NET" alt="Windows CE .NET"/> Windows CE .NET
      </td>
      <td>&mdash;</td>
    </tr>
    <tr>
      <td rowspan="3" align="center"><b><img src="launcher/assets/icons/chip.png" width="16" height="16" title="Chip" alt="Chip"/> Intel SA-1110</b><br/><sub>StrongARM</sub></td>
      <td>
        <img src="launcher/assets/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>HP Jornada 720</b><br/>
        <img src="launcher/assets/icons/os_old_ce.png" width="16" height="16" title="Handheld PC 2000" alt="Handheld PC 2000"/> Handheld PC 2000
      </td>
      <td><img src="launcher/assets/icons/display.png" width="16" height="16" title="Display" alt="Display"/> <img src="launcher/assets/icons/speaker.png" width="16" height="16" title="Sound" alt="Sound"/> <img src="launcher/assets/icons/stylus.png" width="16" height="16" title="Touch" alt="Touch"/> <img src="launcher/assets/icons/internet.png" width="16" height="16" title="Network" alt="Network"/> <img src="launcher/assets/icons/pcmcia.png" width="16" height="16" title="PCMCIA" alt="PCMCIA"/></td>
    </tr>
    <tr>
      <td>
        <img src="launcher/assets/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>iPAQ H3100/H3600/H3700</b><br/>
        <img src="launcher/assets/icons/os_ppc2000.png" width="16" height="16" title="Pocket PC 2000" alt="Pocket PC 2000"/> Pocket PC 2000<br/>
        <img src="launcher/assets/icons/os_ppc2002.png" width="16" height="16" title="Pocket PC 2002" alt="Pocket PC 2002"/> Pocket PC 2002
      </td>
      <td><img src="launcher/assets/icons/display.png" width="16" height="16" title="Display" alt="Display"/> <img src="launcher/assets/icons/speaker.png" width="16" height="16" title="Sound" alt="Sound"/> <img src="launcher/assets/icons/stylus.png" width="16" height="16" title="Touch" alt="Touch"/> <img src="launcher/assets/icons/internet.png" width="16" height="16" title="Network" alt="Network"/> <img src="launcher/assets/icons/pcmcia.png" width="16" height="16" title="PCMCIA" alt="PCMCIA"/></td>
    </tr>
    <tr>
      <td>
        <img src="launcher/assets/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>Siemens SIMpad SL4</b><br/>
        <img src="launcher/assets/icons/os_old_ce.png" width="16" height="16" title="Handheld PC 2000" alt="Handheld PC 2000"/> Handheld PC 2000<br/>
        <img src="launcher/assets/icons/os_ce.png" width="16" height="16" title="Windows CE .NET" alt="Windows CE .NET"/> Windows CE .NET
      </td>
      <td><img src="launcher/assets/icons/display.png" width="16" height="16" title="Display" alt="Display"/> <img src="launcher/assets/icons/stylus.png" width="16" height="16" title="Touch" alt="Touch"/></td>
    </tr>
    <tr>
      <td align="center"><b><img src="launcher/assets/icons/chip.png" width="16" height="16" title="Chip" alt="Chip"/> Intel SA-1100</b><br/><sub>StrongARM</sub></td>
      <td>
        <img src="launcher/assets/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>HP Jornada 820</b><br/>
        <img src="launcher/assets/icons/os_old_ce.png" width="16" height="16" title="Handheld PC 3 (CE 2.11)" alt="Handheld PC 3 (CE 2.11)"/> Handheld PC 3 (CE 2.11)
      </td>
      <td><img src="launcher/assets/icons/display.png" width="16" height="16" title="Display" alt="Display"/> <img src="launcher/assets/icons/mouse.png" width="16" height="16" title="Mouse" alt="Mouse"/> <img src="launcher/assets/icons/keyboard.png" width="16" height="16" title="Keyboard" alt="Keyboard"/> <img src="launcher/assets/icons/internet.png" width="16" height="16" title="Network" alt="Network"/> <img src="launcher/assets/icons/pcmcia.png" width="16" height="16" title="PCMCIA" alt="PCMCIA"/></td>
    </tr>
    <tr>
      <td align="center"><b><img src="launcher/assets/icons/chip.png" width="16" height="16" title="Chip" alt="Chip"/> Microsoft ODO (???)</b><br/><sub>ARM720T (1996 NDA board)</sub></td>
      <td>
        <img src="launcher/assets/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>ODO/Poseidon</b><br/>
        <img src="launcher/assets/icons/os_old_ce.png" width="16" height="16" title="Windows CE 2.11" alt="Windows CE 2.11"/> Windows CE 2.11<br/>
        <img src="launcher/assets/icons/os_old_ce.png" width="16" height="16" title="Windows CE 3" alt="Windows CE 3"/> Windows CE 3
      </td>
      <td><img src="launcher/assets/icons/display.png" width="16" height="16" title="Display" alt="Display"/> <img src="launcher/assets/icons/speaker.png" width="16" height="16" title="Sound" alt="Sound"/> <img src="launcher/assets/icons/stylus.png" width="16" height="16" title="Touch" alt="Touch"/> <img src="launcher/assets/icons/keyboard.png" width="16" height="16" title="Keyboard" alt="Keyboard"/></td>
    </tr>
    <tr>
      <td align="center"><b><img src="launcher/assets/icons/chip.png" width="16" height="16" title="Chip" alt="Chip"/> TI OMAP 3530</b><br/><sub>Cortex-A8</sub></td>
      <td>
        <img src="launcher/assets/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>OMAP 3530 EVM</b><br/>
        <img src="launcher/assets/icons/os_ce.png" width="16" height="16" title="Windows CE 7" alt="Windows CE 7"/> Windows CE 7
      </td>
      <td><img src="launcher/assets/icons/display.png" width="16" height="16" title="Display" alt="Display"/> <img src="launcher/assets/icons/stylus.png" width="16" height="16" title="Touch" alt="Touch"/></td>
    </tr>
    <tr>
      <td align="center"><b><img src="launcher/assets/icons/chip.png" width="16" height="16" title="Chip" alt="Chip"/> Freescale i.MX31L</b><br/><sub>ARM1136</sub></td>
      <td>
        <img src="launcher/assets/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>Zune 30</b><br/>
        <img src="launcher/assets/icons/os_zune.png" width="16" height="16" title="Windows CE 5" alt="Windows CE 5"/> Windows CE 5
      </td>
      <td><img src="launcher/assets/icons/display.png" width="16" height="16" title="Display" alt="Display"/> <img src="launcher/assets/icons/keyboard.png" width="16" height="16" title="Keyboard" alt="Keyboard"/></td>
    </tr>
    <tr>
      <td align="center"><b><img src="launcher/assets/icons/chip.png" width="16" height="16" title="Chip" alt="Chip"/> Samsung S3C2410</b><br/><sub>ARM920T</sub></td>
      <td>
        <img src="launcher/assets/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/> <b>Device Emulator</b><br/>
        <img src="launcher/assets/icons/os_ce.png" width="16" height="16" title="Windows CE 6" alt="Windows CE 6"/> Windows CE 6<br/>
        <img src="launcher/assets/icons/os_ppc2002.png" width="16" height="16" title="Windows Mobile 5" alt="Windows Mobile 5"/> Windows Mobile 5<br/>
        <img src="launcher/assets/icons/os_wm6.png" width="16" height="16" title="Windows Mobile 6" alt="Windows Mobile 6"/> Windows Mobile 6<br/>
        <img src="launcher/assets/icons/os_ppc2002.png" width="16" height="16" title="WM 2003 SE" alt="WM 2003 SE"/> WM 2003 SE<br/>
        <img src="launcher/assets/icons/os_ce.png" width="16" height="16" title="Windows CE 5" alt="Windows CE 5"/> Windows CE 5
      </td>
      <td><img src="launcher/assets/icons/display.png" width="16" height="16" title="Display" alt="Display"/> <img src="launcher/assets/icons/speaker.png" width="16" height="16" title="Sound" alt="Sound"/> <img src="launcher/assets/icons/stylus.png" width="16" height="16" title="Touch" alt="Touch"/> <img src="launcher/assets/icons/keyboard.png" width="16" height="16" title="Keyboard" alt="Keyboard"/> <img src="launcher/assets/icons/internet.png" width="16" height="16" title="Network" alt="Network"/> <img src="launcher/assets/icons/pcmcia.png" width="16" height="16" title="PCMCIA" alt="PCMCIA"/></td>
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

## <img src="gweslab.png" width="24" height="24" /> Claude Development Environment

CERF ships a Claude Code-based development environment for working on the emulator — including bringing up brand-new boards from their ROMs. Launch it from the repo root with:

```
run_claude.cmd
```

It runs Claude Code with a custom system prompt that injects the **entire project documentation** (`CLAUDE.md` plus every `agent_docs/` reference page) into every agent, so each session starts fully briefed on the project's rules, architecture, and subsystems — no "please read the docs first" needed.

The environment provides the **`/start-board-implementation`** skill: drop your ROM into `bundled/devices/` (or just point the agent at it) and run the skill. The agent identifies the board and SoC straight from the ROM, checks what CERF already supports, estimates the effort, and — on your go-ahead — starts the bring-up with a cross-session tracking document. So you can literally drop in your ROM and start the procedure of bringing it up.

> [!WARNING]
> The dev environment runs Claude in skip-permissions mode — it can execute anything on your machine without prompting. It also force-kills its own Claude instance, and **any** `clangd.exe`, that leaks memory past a threshold. The first launch shows a one-time explanation; press Enter to acknowledge it.

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

See [launcher's boards details database](launcher/supported_devices.py) for per-board issues.

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
          <td>v4.1 (NOT RELEASED YET)</td>
          <td>
            <ul>
              <li>HP Jornada 820 support</li>
              <li>Siemens SIMpad SL4 support</li>
              <li>Added HP Palmtop VGA (F1252A) card</li>
              <li>Guest additions: IMGFS injection fixes (e.g. WM >= 6)</li>
              <li>PC Cards: Serial modem emulator and serial forwader</li>
              <li>DDraw path inside Guest Additions (now Zune and friends render, I hope)</li>
            </ul>
          </td>
        </tr>
    <tr>
          <td>v4.0</td>
          <td>
            <ul>
              <li>NE2000 is now hot pluggable in all boards that support PCMCIA</li>
              <li>Compact Flash too with configurator/generator</li>
              <li>iPaq 1st gen now has extensions sleeve emulated (for PCMCIA support)</li>
              <li>iPaq H3100: monochrome screen inversion fixed</li>
              <li>Soft/hard reset your device in Actions menu (might be broken for some SoCs) + corresponding SoC/peripheral updates</li>
              <li>Guest additions: task manager on host - see process list, switch to processes, kill and run right from HOST window</li>
              <li>NE2000 internet delivery hangs are fixed</li>
              <li>Jornada720/SA1110/JIT updates to make it boot Linux-based OS</li>
              <li>Launcher: optional packages feature</li>
              <li>Guest additions: Complete overhaul of XIP injection (Now you can boot Jornada 720 in 4K. Also suddenly Zune 30 is in the game too)</li>
              <li>Various UI/general fixes, improvements</li>
            </ul>
          </td>
        </tr>
    <tr>
          <td>v3.21</td>
          <td>
            <ul>
              <li>iPaq H3100 support</li>
              <li>iPaq H3100,H3600 PPC2002 sound fixes</li>
              <li>Jornada 720 support</li>
              <li>JIT/MMU improvements</li>
            </ul>
          </td>
        </tr>
    <tr>
      <td colspan="2"><b>Previous versions</b> — see the <a href="docs/changelog.html">full changelog</a>.</td>
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

Download WIP build (4.1) from artifacs [![build](https://github.com/gweslab/cerf/actions/workflows/build.yml/badge.svg)](https://github.com/gweslab/cerf/actions/workflows/build.yml) or go to [latest release](https://github.com/gweslab/cerf/releases/latest)
