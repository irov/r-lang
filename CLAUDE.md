@AGENTS.md

# Claude Code in this repository

The rules of AGENTS.md apply in full. These notes are about how a Claude Code session works here.

## Conversation

- The owner writes Russian; answer in Russian. Everything in the tree (code, comments, commit
  messages, READMEs, `compiler/README.md`) stays English, the roadmap and matrix Russian.
- Progress updates are short and say what is running or what was found; do not narrate commands.
- A stage ends with a report: specification revisions and rule counts after the change, every
  defect found (`<STAGE>-<n>`: symptom, cause, fix, test that now catches it), the verification
  table (which presets and audits ran, with counts), and what is left uncommitted or unverified.
  Never report a test as passed that was not run, and never call a failure a flake without the
  stress evidence AGENTS.md asks for.

## Decisions

- Where the roadmap or the owner leaves a design choice open, make it, record it with the reason
  in the matrix section of the stage, and name it in the report so the owner can overturn it.
- Anything that changes the owner's position — a commit, a push, a spec rule removed, a public
  API renamed, a dependency added — is asked about first, every time; an approval covers that
  action only.

## Running things

- Full `ctest` presets and the battery take 10–30 minutes each: run them in the background with
  a watch that reports failures and phase summaries, keep working on the next item meanwhile, and
  never edit the tree they run in.
- Temporary files, logs, probe programs and stress loops go to the session scratchpad, not into
  the repository and not into `/tmp` (the PostgreSQL test drivers create their own short socket
  directories because macOS limits socket paths to 103 bytes).
- Before and after long runs check `ps` for leftover test processes and clusters; stop only what
  this session started.
- When a change is developed in a copy of the tree, copy it back and re-run the gates in the
  repository itself before reporting; the repository and its records are the only durable state.

## Memory

- Claude's auto-memory lives outside the repository and is for the next session, not for the next
  engineer: whatever they need (decisions, defects, verification results, pitfalls) goes into the
  matrix, the roadmap, READMEs or AGENTS.md as well.
