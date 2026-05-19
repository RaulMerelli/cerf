---
name: calc
description: Forcing function — any time the main agent is about to do numeric arithmetic, conversion, bit-mask decoding, or sign-reinterpretation on a value larger than a couple of digits, invoke this skill to route the math through a Bash / PowerShell / Python tool call instead of doing it in prose. The skill does NOT compute the answer; it instructs the agent to compute via tool and retract any hand-computed value it conflicts with. Invoke when the user types `/calc …` or when the agent catches itself about to write a hex value, a decoded bitmask, or a sign-extended integer inline without having run it through a tool first.
---

# Calc — Forcing function for tool-backed arithmetic

You were about to do math in prose. **Stop.** Use a tool.

## The rule

Any of the following goes through a tool call (Bash / PowerShell / Python / `printf`), not your head:

- Decimal ↔ hexadecimal conversion.
- Signed ↔ unsigned reinterpretation at any width ≥ 16 bits.
- Bit-mask decoding — listing which bits are set in a constant, or which named flags a hex value represents.
- Hex arithmetic of any kind.
- Anything involving a 32-bit or larger value.

This applies to both directions:

- Reading a value out of IDA / a decompile / a log and converting it.
- Writing a new constant into CERF code and decoding what bits it represents.

## What to do when invoked

1. State the specific calculation you were about to perform.
2. Run the tool with the exact expression. One-liners:

   ```
   printf '%x\n' <N>                                    # signed decimal → hex
   python -c "print(hex(N & 0xFFFFFFFF))"               # signed 32-bit → hex
   python -c "print(-(~N & 0xFFFFFFFF) - 1)"            # hex → signed 32-bit
   python -c "v=0xXXXXXXXX; print([i for i in range(32) if v & (1<<i)])"   # list set bits
   python -c "print(hex(A | B))"                        # hex arithmetic
   ```

3. Paste the tool's output. That is the canonical answer.
4. **If the tool's answer contradicts a value you already wrote**, retract the previous value in plain language. Do not rationalize the difference. Do not blame the tool. Check your input to the tool first; if the tool really did disagree, the tool wins.

## Anti-patterns (stop signals)

When you catch yourself thinking any of these, invoke this skill immediately:

- *"I know hex, `-N` is obviously `0xX`."*
- *"That's off by a few from what I expected, probably I just need another bit."*
- *"The tool must be lying — let me check the disasm."*
- *"I already computed this once earlier in the session, I'll reuse it."*
- *"Saves a step to just type the answer."*

Every one of these is the moment the cascade begins. Run the tool.

## Why this exists

Mental arithmetic on 32-bit numbers produces wrong answers that read as authoritative — the wrong value often lands on the same "shape" as the right one (two leading bytes match, rough magnitude matches), so the error looks like a legitimate constant. A downstream investigator then builds a whole theory on top ("the reference source must swap these two bits", "this field must mean something else") before anyone questions the original arithmetic. The cascade consumes hours and ships wrong code.

The rule is short enough to recall mid-reasoning: **if I'm typing a number I computed in my head, stop.**
