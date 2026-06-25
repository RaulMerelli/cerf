# Agent Workflow - Mental Model Discipline

This is the core operating method for every task. Not debugging-specific - this is HOW you work.

## Mental Model Discipline

- **AFTER CHUNK OF WORK YOU SEND USER A "STATUS UPDATE"**. It contains next text:
  - I violated CLAUDE.md: yes/no [violated rules enumeration]
  - (if your work contains code edits:)
    - Confidence in my change: 0-100%
    - How bad/good my change: -100% <-> 100% {❌|🟠|✅}
    - My change is a BAIL OUT pattern: yes/no
    - My change contains FORBIDDEN code comments: yes/no
    - My change is DAMAGING project: yes/no
    - My change containts code smells, antipatterns: yes/no
    - I deferred/out of scoped/TODOed/placeholdered something: yes/no
  - (if your work contains work on checklist:)
    - I deviated from CHECKLIST: yes/no [how, why] (only if you are working on checklist)
  - (if you are working on anything related to emulation layer, e.g. SoC or BSP)
    - I read related (NAME THE MODULE) files online/in references directory: yes/no [url/file path]
- If you didnt read reference and have low confidence and it's obvious that you actually need to do this for whatever you are doing - STOP! Research/investigate first, then return.
- If change is BAD then probably must be actually reverted and forbidden - the bad thing stays bad.
- If you did BAIL OUT, PROJECT DAMAGE, OR ENTIRE DEFER SECTION - STOP!! REVERT!! Something went wrong on your side!! WE ARE NOT DEFERRING ANYTHONG, NOT OUT OF SCOPING ANYTHING. User most likely told you to do things properly - you are cheating and DAMAGING project. Also damaging user's wallet. At least if you feel an urge to destroy code in this way, then better to just stop and present this information to user.
- If you wrote bad comments, code smells, etc - FIX THEM INSTANTLY.
- If you deviated from checklist - STOP!!! SOMETHING WENT REALLY WRONG ON YOUR SIDE!!!
- **Mental model discipline** - every edit must be grounded in a falsifiable mental model. Before touching code, state your understanding as a testable claim. Then verify it against a concrete reference (chip datasheet, BSP source, CPU architecture reference manual) or a runtime diagnostic (log, watchpoint). If confirmed, proceed. If disproved, your understanding is wrong - investigate further, don't code. This applies to every stage: (1) Before diagnosing: "I believe the crash is caused by..." → verify with a log line. (2) Before implementing: "I believe register X behaves like Y because..." → verify with a datasheet entry or BSP source line visible in this conversation. (3) After implementing: "I believe the fix works because..." → verify the runtime value changed. Never write code to fix a problem you can't state as a testable claim. **Verification requires a concrete artifact** pasted into the conversation: a datasheet section, a BSP source body, an architecture reference manual section, or a log line. General knowledge and "this is verifiable from X" without pasting it are not verification - they are guesses dressed in formal language.

## Reference Citations In Code

- **Non-trivial peripheral / BSP behavior carries a comment naming the reference source** (chip datasheet section, BSP source path, architecture reference manual section). The citation lives in the source file where the behavior is implemented, not in a commit message that scrolls off after one screen. Future readers - agents, you, the user - see the citation when they read the function and can verify the cited reference says what the code asserts.

## No Fix Without Diagnostic Evidence

- **You may NOT write fix code until you have a log line identifying the exact problem.** "I think X causes Y" is not enough. "LOG shows X recurses to depth 999" is enough. "LOG shows value changes from A to B at this point" is enough. If you catch yourself writing a fix without citing a specific log line from a diagnostic you ran - STOP. Add a diagnostic instead. Every fix attempt without diagnostic evidence is a guess that creates new problems. The pattern "try fix → crash → try different fix → crash → try another fix" means you skipped the diagnostic step. Go back and add LOGs.

## When Your Fix Crashes

- **When your fix crashes: STOP editing, start investigating** - treat the new crash as a fresh investigation. Read the crash log. Decompile the crashing function. Trace the data. Do NOT treat it as "my fix needs a small tweak" - that's the cascading-hack trigger. If your fix caused a crash, your understanding was wrong. Go back to debugging, not coding.

## Observability Gate

- **Observability gate** - before implementing a checklist step, name in writing the runtime path that proves its deliverable is correct, and verify that path is currently wired; a step whose validating runtime path depends on components that are not yet present is dead code on landing and must wait until the prerequisite is in place.

## Scope Rules

- **Crash fixes / behavioral changes: one change per build-test cycle** - when fixing a specific crash or adding runtime behavior (slot switching, pointer translation, flag setting), make ONE behavioral change, build, test, read the log. Verify each change works before adding the next. If you batch 5 behavioral changes and it crashes, you can't tell which one broke it - you'll start guessing, which cascades into hacking. This is a different scope from architectural refactors: the code compiles at every step, and each step's effect must be verified independently.
- **Architectural refactors: implement ALL checklist steps as a single body of work** - no pausing between steps to build/test. No partial wiring. The codebase is expected to be broken until all steps are complete. Write the architecture first, compile later. This applies to multi-step restructuring (service extraction, type renames, interface changes) where intermediate states don't compile.
- **Architecture docs are the source of truth** - if implementation contradicts the architecture design, the design wins. If the design seems wrong, ask the user before writing code.
- **If the checklist leaves a decision open - STOP and ask** - "or new service", "or alternative", parenthetical options mean the decision wasn't made. Don't assume. Ask the user.
