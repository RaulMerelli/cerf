# **CE Runtime Foundation** v{version}

<p align="center">
  <a href="https://cerf.cx">
    <img src="gweslab.png" width="400" alt="cerf.cx" />
  </a>
</p>

<p align="center">
  <b><a href="https://cerf.cx">cerf.cx</a></b> - more information about the project
</p>

<br/>

A universal Windows CE emulator. It is a virtual hardware platform that boots real CE and Windows Mobile ROMs on modern Windows.

> [!WARNING]
> **Beta stage.** CERF is a one-person open source project. Expect bugs and
> breaking changes.

> [!CAUTION]
> **CERF 6.8 is temporary broken.** We remove third-party licensed
> code from the emulator core and write new code from primary documentation
> The current version still does not correspond to CERF 6.7 level of stability and performance.

[![Discord](https://img.shields.io/badge/Discord-join%20the%20server-5865F2?logo=discord&logoColor=white)](https://discord.gg/QREE9Y2v2d) {support_badges}

## Downloads

To use the newest features, download the WIP build ({version}) from the artifacts [![build](https://github.com/gweslab/cerf/actions/workflows/build.yml/badge.svg)](https://github.com/gweslab/cerf/actions/workflows/build.yml). For a stable version, go to the [latest release](https://github.com/gweslab/cerf/releases/latest).

Run **`launcher.exe`** and select a device. The launcher downloads the ROM bundle and boots it. The [articles](https://cerf.cx/articles/command-line/) show how to run `cerf.exe --device=...` directly, and describe its command line and its logs.

## Supported boards

{supported_devices}

## Running your own ROM

A ROM boots only if **CERF implements that exact board**. A matching SoC is not sufficient.

**The board is on the supported list.** The [articles](https://cerf.cx/articles/own-rom/) show how to boot your own dump.

**The board is not on the supported list.** A new board is a code contribution. It needs C++ for the memory map of the board, for each peripheral that the drivers use, and for the quirks of the SoC. The code must agree with datasheets, BSP sources and reverse engineering, at the quality level of the current tree. A new board is not a change to a configuration file - that's not that simple.

> [!IMPORTANT]
> **CERF does not accept ROM submissions or requests for new boards.** Send a contribution, or use a board that CERF supports. The project also adds a board on its own sometimes, when the board is important for historical preservation, or interesting.

## Building

CERF requires Visual Studio 2026 with the C++ desktop development workload.

> [!NOTE]
> **The first build on a new machine takes more than one hour.** vcpkg compiles the dependencies from source before CERF links. This occurs one time on each machine. Later builds use the cached `vcpkg_installed/` tree and are complete in a few minutes. Do not stop the first build.

Configure the clone (one time on each machine):

```
setup.cmd
```

This script initializes the submodules. It points git at the tracked hooks of the
repo (`core.hooksPath` = `.githooks`). Git does not clone the hook configuration,
so the hooks do nothing in a new clone until you run this script. The script also
reports each missing prerequisite (the Python launcher, the vcpkg MSBuild
integration). You can run it again at any time, because it is idempotent.
`setup.cmd -Check` reports the status and changes nothing.

Build with the helper script:

```
powershell -ExecutionPolicy Bypass -File build.ps1
```

Or run msbuild directly:

```
msbuild cerf.sln /p:Configuration=Release /p:Platform=Win32
```

### Building the CE-side binaries (optional)

`ce_apps/` holds the Windows CE binaries that CERF ships, and the Guest Additions
display driver. To build them, you need a CE toolchain and a CE SDK. `cerf.exe`
does **not** need them. If you work on the emulator core, the boards, the SoCs,
the JIT or the host UI, use the prebuilt binaries.

To build them, install eMbedded Visual C++ 4.0 (a free Microsoft download from the
Microsoft archive). Then run one script. The full instructions are in
**[docs/ce_apps_setup.md](docs/ce_apps_setup.md)**.

`setup.cmd -Check` reports whether the CE toolchain is present.

CERF builds the website from `docs/website/`. The command `python tools/build_site.py --serve` runs the website on your machine with live reload.

## Changelog

{changelog}

## Known Issues

For the issues of each board, see the [board database of the launcher](launcher/supported_devices.py).

## Claude Development Environment

The project is primarily built with help of [Claude](https://claude.ai) and [Claude Code](https://docs.anthropic.com/en/docs/claude-code).

> [!CAUTION]
> AI written code (and well - the human written code too) might include mistakes a developer did not notice. Be careful when using it as a reference for peripherals and other hardware level systems.

---

CERF includes a development environment that uses Claude Code. You can work on the emulator with it, and you can add new boards from their ROMs. Run it from the root of the repo:

```
run_claude.cmd
```

This environment runs Claude Code with a custom system prompt. The prompt puts the **full project documentation** into each agent (`CLAUDE.md` and each reference page in `agent_docs/`). Thus each session starts with the rules, the architecture and the subsystems of the project. You do not tell the agent to read the documentation first.

The environment gives you the **`/start-board-implementation`** skill. Put your ROM into `bundled/devices/`, or give the agent the path to it. Then run the skill. The agent identifies the board and the SoC from the ROM. It then examines what CERF supports and estimates the work. If you agree, the agent starts the work and writes a tracking document that stays between sessions.

> [!WARNING]
> The development environment runs Claude in skip-permissions mode. Claude can run any command on your machine, and it does not ask you first. The environment also stops its own Claude instance, and **any** `clangd.exe`, that uses more memory than a limit. At the first start, it shows an explanation one time. Press Enter to accept it.

## License

[MIT](LICENSE). Third-party components and studied references are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
