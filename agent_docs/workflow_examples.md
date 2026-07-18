# Workflow Examples

Proven investigation recipes. Reach for the matching one before inventing your own.

## Parallel IDA sweep - one question across many guest binaries

Use when a question spans many modules at once: "which driver owns SYSINTR N",
"who touches register X", "does ANY module in the ROM validate this value".

1. Extract per-module PEs if absent
   (`references/extracted-roms/<device>/<rom>/fs/Windows/`, see debugging.md § IDA discipline).
2. Preload EVERY needed IDA instance yourself: `python tools/open_ida.py --wait <module>`;
   confirm each prints its port + "IDA IS READY!".
3. Verify the stack with `mcp__ida_mcp__ida_list_instances`; note each module's port.
4. Spawn parallel subagents restricted to `mcp__ida_mcp__*` tools ONLY - no Bash, no
   PowerShell, no file ops, no opening/closing IDA instances. Each prompt names its exact
   ports and 1-3 modules, and requires: an IDA address cited for every claim; UNDETERMINED +
   blocking reason instead of a guess; raw-data output, not prose.
5. Cross-check contradictions between agent reports with your own targeted decompile/disasm
   before trusting either side.
6. Persist the consolidated map into the investigation's durable document (the tracking doc,
   or a map file it points at) BEFORE calling the sweep done.
