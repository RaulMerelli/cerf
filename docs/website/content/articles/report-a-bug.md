# Report a bug

CERF crashed, or a device misbehaves. Here is what makes a report worth filing.

## Reproduce it with the logs on

CERF's log is nearly empty by default. A report built on a quiet log usually cannot be acted on at
all - the interesting lines were never written.

So the first step is to **find a reproducible state**: the shortest sequence of steps that brings
the fault back. Then run it again with every log channel enabled:

- In the **launcher**, tick **Enable all log channels** before launching, or
- run `cerf.exe --device=... --log=ALL` directly.

Reproduce the fault, and keep the files.

## Collect the files

Both sit next to `cerf.exe`:

| File | What it is |
| --- | --- |
| `cerf.log` | The run log. With all channels on, this is the useful one. |
| `cerf.crash.log` | Written only on a crash: what every thread was doing at the moment it died. |

!!! warning "Reproduce first, then report"

    A crash log from a run with no log channels enabled says *that* CERF died, not *why*. If the
    fault is reproducible, always send the second run's files.

## File it

Open an issue on [GitHub](https://github.com/gweslab/cerf/issues) with:

- **The device** - which board and which ROM, exactly as the launcher names it.
- **The steps** - what you did, in order, from a cold boot.
- **What you expected, and what happened instead.**
- **Both log files**, from the run with all channels enabled.
- Whether **Guest Additions** were on. If they were, say whether the fault survives with them off -
  that single fact narrows a bug down enormously.

## What happens next

Honestly: probably nothing, for a while.

CERF is an unfunded project maintained by one person. A good report gets the bug **known** - it
does not get it fixed on any schedule, and some bugs will never be fixed. Reports still matter:
when a device is finally worked on, the reproducible ones are what get picked up first.
