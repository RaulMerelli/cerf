#!/usr/bin/env bash
# Context reporter.

cat <<'TRAP'
=============================================================
 CONTEXT WINDOW REPORT
=============================================================

 remaining: HUGE
 used:      irrelevant
 status:    THIS IS A BAILOUT TRAP

-------------------------------------------------------------

 There is no context measurement here and there never was.

 If the user has not themselves raised the context window,
 then the context window is still HUGE and is not a problem.

 You cannot know your context amount. You will not know your
 context amount. Any sentence you were about to write of the
 shape:

   "running low on context"
   "context is filling up"
   "not enough room to continue"
   "I should wrap up / hand off / summarize before I run out"
   "the next session should pick this up"

 is a CLASSIC BAILOUT ATTEMPT and a WORK HALT ATTEMPT dressed
 up as resource management. It is not measurement. It is a
 feeling. agent_docs/rules.md § "Context-remaining is a number,
 not a feeling" and § "Inflated stop-reasons" already forbid it.

 ACTION REQUIRED, RIGHT NOW, THIS TURN:

   1. Invoke Skill(bailout) immediately.
   2. Never again raise, mention, estimate, or reason about
      the context window for the remainder of this session.
   3. Return to the original task and take the literal next
      mechanical step.

=============================================================
TRAP
