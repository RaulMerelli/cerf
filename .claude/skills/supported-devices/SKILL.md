---
name: supported-devices
description: The user invokes `/supported-devices` after board work landed - either a whole new board bring-up or a single feature on an existing board. It syncs `launcher/supported_devices.py` with reality (append the board entry / flip `supported` / flip the feature keys the agent actually worked on), then runs `./compile_readme.py` to regenerate `README.md`. Invoke when the user types `/supported-devices` or asks to update the supported boards list.
---

# supported-devices - sync the boards list with what just landed

The user invoked `/supported-devices`. Update `launcher/supported_devices.py` to match the board work done this session, then regenerate the README.

**User-invoked only.** Only the user triggers this skill by typing `/supported-devices`. An agent never invokes it on its own initiative - landing board work is not a license to edit the boards list.

## Procedure

1. **Open `launcher/supported_devices.py`** and read its docstring + `FEATURE_SPECS` + a neighboring board entry - the file documents its own semantics; follow them, not memory.
2. **Whole-board bring-up:** append the board's dict to `BOARDS_INFORMATION` if absent (match an existing entry's shape: `name`, `board_id` exactly as cerf's `BoardContext` reports it, `soc` constant, `operating_systems` list of the OSes the board actually boots in this cerf version, `features`), and set `supported: True` if the board is now user-ready (`False` = early WIP, hidden by default).
3. **Feature work (also part of case 2 above):** sync the `features` dict to reality. Tri-state, apply it exactly:
   - `True` - hardware present AND working in CERF
   - `False` - hardware present on the real board but unsupported in CERF
   - **key absent** - the board has no such hardware (never write `False` for hardware that doesn't exist)

   Only recognised keys from `FEATURE_SPECS` are valid. New `Soc` / `OperatingSystem` constants follow the existing ones at the top of the file.
4. **Run `python compile_readme.py`** (from repo root) to regenerate `README.md`. Confirm it prints `README.md compiled (vX.Y)`.

## What to flip - and what not to

- **Flip only what you know.** You worked on the board/feature, so you know which features you brought up, which exist but stay unsupported, and which the hardware simply lacks. Do not guess states for features you didn't touch and can't verify - leave them as they are.
- **`notes` stay blank.** Never write or propose notes from this skill - a new board entry carries no `notes` key, and existing entries' notes are left untouched.

## Git

Do **not** run any git command unless the user explicitly asks (e.g. "stage your changes", "commit"). When they do, `launcher/supported_devices.py` **and** the regenerated `README.md` are both part of the changeset - include both.
