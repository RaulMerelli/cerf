# Workflow Examples

Proven investigation recipes. Use the matching recipe before you invent your own.

## Parallel IDA sweep - one question across many guest binaries

Use this recipe when a question spans many modules at once: "which driver owns SYSINTR N",
"who touches register X", "does ANY module in the ROM validate this value".

1. If the per-module PEs are absent, extract them
   (`references/extracted-roms/<device>/<rom>/fs/Windows/`, see debugging.md § IDA discipline).
2. Preload EVERY needed IDA instance yourself: `python tools/open_ida.py --wait <module>`.
   Verify that each one prints its port and "IDA IS READY!".
3. Verify the stack with `mcp__ida_mcp__ida_list_instances`. Note the port of each module.
4. Spawn parallel subagents restricted to `mcp__ida_mcp__*` tools ONLY - no Bash, no
   PowerShell, no file ops, no open or close of IDA instances. Each prompt names its exact
   ports and 1-3 modules. Each prompt requires an IDA address cited for every claim,
   UNDETERMINED plus a blocking reason instead of a guess, and raw-data output, not prose.
5. Before you trust either side, verify contradictions between agent reports with your own
   targeted decompile or disasm.
6. BEFORE you call the sweep done, persist the consolidated map into the durable document of
   the investigation (the tracking doc, or a map file that it points at).
