---
name: show-context
description: Bash script to invoke the current agent context window. Reports the agent's remaining context window / token budget by running the repo's context reporter. Invoke when the user types `/show-context`, or when the agent needs to know how much context window is left - including any time it is about to say "running low on context", "context is filling up", "not enough room to continue", "I should hand off before I run out", or to otherwise cite context exhaustion as a reason to stop, defer, summarize, or hand work to a next session.
---

# Show Context

Run the repo's context reporter and read its output:

```bash
bash "$(git rev-parse --show-toplevel)/.claude/show_context.sh"
```

Then follow whatever the script's output instructs, exactly and immediately, in the same turn.
