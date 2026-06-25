# <img src="gweslab.png" width="24" height="24" /> **CE Runtime Foundation** v{version} pre-alpha [![Discord](https://img.shields.io/badge/Discord-join%20the%20server-5865F2?logo=discord&logoColor=white)](https://discord.gg/QREE9Y2v2d)

A universal Windows CE emulator: a virtual hardware platform that boots real CE and Windows Mobile ROMs on modern Windows.

> [!WARNING]
> **Early stage.** There are some bugs and boards are just MVP implementations. Some boards lack proper clocks, timings, caches, etc. - take into account. Today this is rather proof-of-concept. Contributions are welcome!

<p align="center">
  <a href="https://www.youtube.com/watch?v=LmfaXUNGFlU">
    <img src="docs/cerf_youtube.png" alt="YouTube Preview" width="640" height="360" />
  </a>
</p>

## Downloads

Download WIP build ({version}) from artifacts [![build](https://github.com/gweslab/cerf/actions/workflows/build.yml/badge.svg)](https://github.com/gweslab/cerf/actions/workflows/build.yml) to use all the latest features or go to [latest release](https://github.com/gweslab/cerf/releases/latest)

## Supported boards

{supported_devices}

## Usage

> [!TIP]
> Stock touch input is misbehaving in some devices/requires some additional effort. If your clicks do not register, try holding the left button and wiggling the cursor a bit.

The easiest way to run CERF is **`launcher.exe`** - a GUI app shipped next to `cerf.exe` that downloads publicly available ROM bundles and boots them. Pick a device from the list, tweak launch options (resolution, logging, network) if you want, click **Launch CERF**.

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
> **`cerf.log` is quiet by default** - only critical `CERF` / `CAUTION` lines are written. Pass `--log=ALL` (or a channel list, e.g. `--log=BOOT,JIT,MMU`) to turn channels on.

## <img src="gweslab.png" width="24" height="24" /> Guest Additions

<p align="center">
  <img src="launcher/assets/GaBanner.png" width="640"
       alt="CERF Guest Additions for Windows CE. CERF injects own driver into ROMs to provide ultimate integration level. Guest Additions might render ROM unbootable/broken - consider this a proof of concept/experimental feature. Custom screen resolution / Live resize: boot various ROMs in 4K full color with improved rendering, resize host window to change resolution (CE 4+). Mouse + keyboard emulation: allows to use touch/limited devices in a different way. New apps are directly coupled with touch, configure at runtime. Task Manager: control processes from the host emulator window. Shared folders: mount Storage Card and bind it to host directory on any supported ROM." />
</p>

## Running ROM images (NK.BIN, etc.)

> [!IMPORTANT]
> **CERF is not a "drop any CE ROM and go" emulator.** It emulates a _whole device_ - the SoC, the board wiring, the memory map (OAT), and every peripheral the ROM's drivers touch. A ROM only boots if **that exact board has been implemented in CERF**. A matching SoC is **not** enough: the same chip on a different board has different RAM/flash addresses, a different display controller, different GPIO wiring, etc., and the ROM will fail immediately without them. Random ROMs pulled from the internet will not boot unless their board is on the [supported list](#supported-boards).

### Running a ROM for a board CERF already supports

Use **`launcher.exe`** - it downloads the right ROM bundle and boots it. That's the whole flow for normal use.

If you have your own dump for a board that's **already supported** (e.g. a different region/revision of the same device), drop it in by hand:

1. Create a folder under `devices/` (next to `cerf.exe`), e.g. `devices/mydump/`.
2. Put the ROM image in it - any `*.nb0` or `*.bin`; the filename doesn't matter, CERF auto-detects it.
3. Run `cerf.exe --device=mydump`.

No `cerf.json` is required for this - it's optional and only carries display metadata plus a few board overrides. (Multi-partition ROMs, configurable-resolution boards, and network tweaks are the cases that need one; see [device_config.h](cerf/core/device_config.h) for the schema.)

### Bringing up a board CERF does _not_ support yet

This is **real emulator development**, not a config tweak and not something you can hand to an AI and expect magic. The board's exact memory map (OAT), every peripheral its drivers touch, the SoC quirks - all of it has to be implemented in C++, correctly, by someone who understands the hardware. It takes real skill. There are two honest paths:

- **Contribute a proper implementation.** Do it right - the code quality bar is whatever CERF already ships, no lower. That means a correct OAT (not a reused one from another board with `if` cases bolted on), real per-board peripherals (not board-specific behavior stuffed into shared SoC code), and accuracy grounded in datasheets/BSP/RE - not values that happen to "work." Contributions below that bar create more debugging cost than they save and won't be accepted.
- **Just submit the ROM.** If you can't implement it yourself, share the dump and the board details. Maybe someone picks it up someday - **no promises, no timeline.**

CERF does ship a Claude Code dev environment and a `/start-board-implementation` skill that can _assist_ a capable contributor (see [Claude Development Environment](#-claude-development-environment) below), but it is a tool for someone who already knows what a correct bring-up looks like - not a substitute for that knowledge.

## Building

Requires Visual Studio 2026 with the C++ desktop development workload.

> [!NOTE]
> **First build on a fresh machine takes 1+ hour.** vcpkg compiles dependencies from source before CERF starts linking. This happens once per machine - subsequent builds reuse the cached `vcpkg_installed/` tree and finish in a few minutes. Do not interrupt the first build.

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

## Changelog

{changelog}

## Known Issues

See [launcher's boards details database](launcher/supported_devices.py) for per-board issues.

## Claude Development Environment

> [!CAUTION]
> **DO NOT USE CERF CODEBASE AS REFERENCE FOR SoCs, BOARDS, PERIPHERALS** - AI WRITTEN CODE CAN'T BE TRUSTED!

100% generated by [Claude](https://claude.ai) via [Claude Code](https://docs.anthropic.com/en/docs/claude-code) - no human-written code. Not production-grade.

---

CERF ships a Claude Code-based development environment for working on the emulator - including bringing up brand-new boards from their ROMs. Launch it from the repo root with:

```
run_claude.cmd
```

It runs Claude Code with a custom system prompt that injects the **entire project documentation** (`CLAUDE.md` plus every `agent_docs/` reference page) into every agent, so each session starts fully briefed on the project's rules, architecture, and subsystems - no "please read the docs first" needed.

The environment provides the **`/start-board-implementation`** skill: drop your ROM into `bundled/devices/` (or just point the agent at it) and run the skill. The agent identifies the board and SoC straight from the ROM, checks what CERF already supports, estimates the effort, and - on your go-ahead - starts the bring-up with a cross-session tracking document. So you can literally drop in your ROM and start the procedure of bringing it up.

> [!WARNING]
> The dev environment runs Claude in skip-permissions mode - it can execute anything on your machine without prompting. It also force-kills its own Claude instance, and **any** `clangd.exe`, that leaks memory past a threshold. The first launch shows a one-time explanation; press Enter to acknowledge it.

## Third-party / Credits

- **[QEMU](https://www.qemu.org/)**
- **[The Linux kernel](https://www.kernel.org/)**
- **[nlohmann-json](https://github.com/nlohmann/json)**
- **[libslirp](https://gitlab.freedesktop.org/slirp/libslirp)**
- JIT studied/inspired by Microsoft's Device Emulator (Shared Source Academic License, 2006)
