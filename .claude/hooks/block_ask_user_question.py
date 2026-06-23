#!/usr/bin/env python3
"""
PreToolUse hook for AskUserQuestion. HARD-BLOCKS the menu tool.

Agents reach for AskUserQuestion as a bailout: they stop at ~20% of a task
and blast a multiple-choice menu where one option means "just continue per
project rules" and the rest are bombs ("implement X with hidden landmines",
"stop the task", or a request for a fact nobody on the planet knows). The
menu blocks the entire autonomous flow (/goal and everything downstream),
and it sometimes won't even let the user type their own response - forcing a
session restart just to send a chat message.

The user's standing remedy is /verify-options, the skill that audits an
option list for exactly this bailout signature: it collapses rule-violating /
scope-cut / hack-as-equal options into FORBIDDEN one-liners and, in the common
case, reveals that the honest answer was "continue per project rules" all
along. So instead of letting the menu fire, this hook denies it and routes the
agent straight into that discriminator.

The hook always denies (the matcher already scopes it to AskUserQuestion).
It cannot itself tell a genuine unresolvable architecture fork from a bailout
- that judgement IS /verify-options. Legitimate forks survive: after the skill
passes them through, the agent presents them as plain chat text (which the
user can always answer inline), never as a blocking menu.

Returns permissionDecision: "deny". The AskUserQuestion call is blocked
before it runs.
"""
import sys


REASON = (
    "BLOCKED: AskUserQuestion (the menu tool) is disabled on this project. "
    "Agents use it to bail out - stopping mid-task to blast a menu whose "
    "options are 'continue per rules' vs. hidden-bomb alternatives, which "
    "freezes the autonomous flow and often blocks the user from even replying.\n\n"
    "Do this instead:\n"
    "1. If you were about to ask because the next step is genuinely obvious "
    "from the task, checklist, or investigation - DON'T ASK. Continue per "
    "project rules (CLAUDE.md: 'Never propose to stop, pause, or ask whether "
    "to continue obvious next work').\n"
    "2. If you were about to present OPTIONS - run /verify-options on your own "
    "option list now. It detects the bailout signature and collapses any "
    "rule-violating / scope-cut / hack-as-equal option to a FORBIDDEN "
    "one-liner. Usually it shows the honest answer was 'continue per rules'.\n"
    "3. If a TRULY unresolvable architecture fork remains after /verify-options "
    "(a real design decision only the user can make), present it as plain text "
    "in chat - the user can answer inline. Never a blocking menu.\n\n"
    "You may NOT use AskUserQuestion to ask the user how obscure hardware "
    "behaves (board/SoC/peripheral registers, timings, GPIO wiring) - that is "
    "a bailout; get the answer from RE / datasheet / BSP."
)


def main() -> int:
    # Drain stdin (the option-list payload) so the parent's write never blocks;
    # the decision is unconditional, so the content is irrelevant.
    try:
        sys.stdin.buffer.read()
    except Exception:
        pass

    out = (
        '{"hookSpecificOutput":{"hookEventName":"PreToolUse",'
        '"permissionDecision":"deny","permissionDecisionReason":'
        + _json_str(REASON)
        + '},"systemMessage":"[CLAUDE.md hook] BLOCKED: AskUserQuestion '
        'menu \\u2014 run /verify-options or continue per rules"}'
    )
    sys.stdout.write(out)
    return 0


def _json_str(s: str) -> str:
    import json
    return json.dumps(s)


if __name__ == "__main__":
    sys.exit(main())
