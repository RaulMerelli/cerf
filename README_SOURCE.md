# **CE Runtime Foundation** v{version} pre-alpha

<p align="center">
  <a href="https://cerf.cx">
    <img src="gweslab.png" width="400" alt="cerf.cx" />
  </a>
</p>

<p align="center">
  <b><a href="https://cerf.cx">cerf.cx</a></b> - read more about the project
</p>

<br/>

A universal Windows CE emulator: a virtual hardware platform that boots real CE and Windows Mobile ROMs on modern Windows.

[![Discord](https://img.shields.io/badge/Discord-join%20the%20server-5865F2?logo=discord&logoColor=white)](https://discord.gg/QREE9Y2v2d) {support_badges}

## Downloads

Download the WIP build ({version}) from artifacts [![build](https://github.com/gweslab/cerf/actions/workflows/build.yml/badge.svg)](https://github.com/gweslab/cerf/actions/workflows/build.yml) to use all the latest features, or go to the [latest release](https://github.com/gweslab/cerf/releases/latest).

Run **`launcher.exe`**: pick a device, and it downloads the ROM bundle and boots it. Running `cerf.exe --device=...` directly, its command line and its logs are covered [in the articles](https://cerf.cx/articles/command-line/).

## Supported boards

{supported_devices}

## Running your own ROM

A ROM boots only if **that exact board is implemented in CERF** - a matching SoC is not enough. Dropping in your own dump of an already-supported board is covered [in the articles](https://cerf.cx/articles/own-rom/).

Bringing up a board CERF does **not** support is emulator development: the board's memory map, every peripheral its drivers touch, the SoC quirks - all of it implemented in C++, grounded in datasheets, BSP sources and RE, at the quality bar CERF already ships. It is not a config tweak, and not something to hand to an AI and expect magic.

> [!IMPORTANT]
> **CERF does not accept ROM submissions / board implementation requests.**
> The devices worth doing are done, and so are several that cost months of work and that essentially nobody needs. Bringing up one more board to arrive at one more Windows CE desktop is a very large amount of work for an outcome that already exists. **Further submissions will be declined**, unless they are genuinely interesting or in demand and I want to do the work.

## Building

Requires Visual Studio 2026 with the C++ desktop development workload.

> [!NOTE]
> **First build on a fresh machine takes 1+ hour.** vcpkg compiles dependencies from source before CERF starts linking. This happens once per machine - subsequent builds reuse the cached `vcpkg_installed/` tree and finish in a few minutes. Do not interrupt the first build.

Set up the clone (once per machine):

```
setup.cmd
```

This initialises submodules, points git at the repo's tracked hooks
(`core.hooksPath` = `.githooks` - git does not clone hook config, so hooks are
inert in a fresh clone until this runs), and reports any missing prerequisite
(Python launcher, vcpkg MSBuild integration). Re-run it any time; it is
idempotent. `setup.cmd -Check` reports status without changing anything.

Build via the helper script:

```
powershell -ExecutionPolicy Bypass -File build.ps1
```

Or invoke msbuild directly:

```
msbuild cerf.sln /p:Configuration=Release /p:Platform=Win32
```

### Building the CE-side binaries (optional)

`ce_apps/` holds the Windows CE binaries CERF ships, including the Guest Additions display
driver. Building them needs a CE toolchain and SDK, which are **not** required for
`cerf.exe` itself - if you are working on the emulator core, boards, SoCs, the JIT or the
host UI, ignore this and use the prebuilt binaries.

To build them, install eMbedded Visual C++ 4.0 (a free, officially archived Microsoft
download) and run one script. Full instructions: **[docs/ce_apps_setup.md](docs/ce_apps_setup.md)**.

`setup.cmd -Check` reports whether the CE toolchain is present.

The website is built from `docs/website/` - `python tools/build_site.py --serve` runs it locally with live reload.

## Changelog

{changelog}

## Known Issues

See [launcher's boards details database](launcher/supported_devices.py) for per-board issues.

## Claude Development Environment

> [!CAUTION]
> **DO NOT USE CERF CODEBASE AS REFERENCE FOR SoCs, BOARDS, PERIPHERALS** - AI WRITTEN CODE CAN'T BE TRUSTED!

Built with [Claude](https://claude.ai) via [Claude Code](https://docs.anthropic.com/en/docs/claude-code). Not production-grade.

---

CERF ships a Claude Code-based development environment for working on the emulator - including bringing up brand-new boards from their ROMs. Launch it from the repo root with:

```
run_claude.cmd
```

It runs Claude Code with a custom system prompt that injects the **entire project documentation** (`CLAUDE.md` plus every `agent_docs/` reference page) into every agent, so each session starts fully briefed on the project's rules, architecture, and subsystems - no "please read the docs first" needed.

The environment provides the **`/start-board-implementation`** skill: drop your ROM into `bundled/devices/` (or just point the agent at it) and run the skill. The agent identifies the board and SoC straight from the ROM, checks what CERF already supports, estimates the effort, and - on your go-ahead - starts the bring-up with a cross-session tracking document. So you can literally drop in your ROM and start the procedure of bringing it up.

> [!WARNING]
> The dev environment runs Claude in skip-permissions mode - it can execute anything on your machine without prompting. It also force-kills its own Claude instance, and **any** `clangd.exe`, that leaks memory past a threshold. The first launch shows a one-time explanation; press Enter to acknowledge it.

## License

[MIT](LICENSE). Third-party components and studied references are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
