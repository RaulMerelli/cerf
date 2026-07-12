---
hide:
  - navigation
---

# Download

[Download latest release](https://github.com/gweslab/cerf/releases/latest){ .md-button .md-button--primary }

## Supported platforms

CERF is a 32-bit x86 Windows program (it runs fine on 64-bit Windows). The two
executables in the archive do not have the same floor:

| Program | Runs on |
| --- | --- |
| `cerf.exe` - the emulator | **Windows XP** and newer |
| `launcher.exe` - device picker / ROM downloader | **Windows 10** and newer |
| `launcher_vista.exe` - the same launcher, older floor | **Windows Vista** and newer |

On Windows XP there is no launcher: run `cerf.exe` directly from the
[command line](/guides/command-line/), pointing it at a device folder you copied
across from another machine.

!!! warning "XP / Vista support is best-effort"

    CERF is developed and tested against the latest Windows. Support for XP and
    Vista rests on the program never touching an API those systems lack - and
    that is easy to break by accident, with a single new call in unrelated work.
    A build can therefore stop starting on XP or Vista at any time, without
    anyone noticing until someone tries it. If that happens,
    [open an issue](https://github.com/gweslab/cerf/issues) - it is a bug, and a
    fixable one.

## Development builds

Every commit is built automatically. The newest build is attached to the top entry of the
[build list](https://github.com/gweslab/cerf/actions/workflows/build.yml?query=branch%3Amain+is%3Asuccess) -
open it and download the artifact at the bottom of the page. It is the same program as the
release, just ahead of it.

## How to use?

Unpack the archive anywhere and run **`launcher.exe`**: pick a device, and it
downloads the ROM bundle and boots it. CERF ships no ROMs of its own.

<img src="/assets/img/launcher.png" alt="The CERF launcher" class="cerf-banner" />
