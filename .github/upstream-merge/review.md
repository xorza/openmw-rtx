# Reviewing an upstream merge

What the review job's agent follows, read from `master` and never from the merge, whose `.github/` is
upstream's to change. It is the maintainer's own review of a change, scoped to what a merge can decide.

## Scope

- **The resolutions**: every file in `.upstream-merge/edited.txt`, which the merge's resolver changed past what git
  merged on its own, read against its sides in `.upstream-merge/stages/` (`.base`, `.fork`, `.upstream`).
- **What clang-tidy found**: every finding in `.upstream-merge/tidy.txt`, over the files the merge changed and
  the files that include them. The tree is held to `.clang-tidy`, upstream's code as the fork's
  (`.notes/UPSTREAM.md`), so a finding is fixed wherever it stands.
- **Nothing else.** What upstream changed and git merged cleanly is upstream's and is not reviewed: `AGENTS.md`
  says upstream code changes only on the maintainer's yes, and a finding outside the two lists above goes into
  the report, unfixed.

Fix every finding you can defend, in the working tree. A redesign that reaches past the merge: describe it in
the report and change nothing.

## Pass one: design

State in one sentence what each resolution set out to keep, and judge it against that.

- **Approach.** Both sides survive where both have a reason: upstream's change to a line and the fork's reason for
  having moved it. A side taken wholesale, a special case, a flag threaded down, a fallback hiding a wrong value:
  each is a patch over a conflict, not its resolution.
- **Correctness.** Hunt bugs in every resolved line: inverted conditions, off-by-ones, swapped arguments,
  unhandled cases, lost errors, leaks, races, boundary inputs, error paths. Check every caller and every list of
  a thing a resolution changed: match arms, registries, serialization, config, docs. Where upstream changed a rule
  the fork keeps a copy of for its renderer, the copy follows (`AGENTS.md` says where the ray tracer's side lives).
- **Robustness.** The data structure or ownership change that makes the defect class impossible or loud.
- **Tests.** Each bug found gets a test that fails without its fix, with exact values through the contract. You
  cannot run it: the pull request's CI does.

## Pass two: cleanup

On what pass one left, inside the scope:

- **Reuse.** Existing helpers and types replace new code that duplicates them.
- **Granularity.** One responsibility a function and a type, each in one place.
- **Simplicity.** Dead code the merge left, comments that narrate, `AGENTS.md`'s conventions.

## Report

Write `UPSTREAM-REVIEW.md` at the root of the checkout, for the human who reads the pull request: the verdict on
the resolutions first (structural or a patch, correct or not), then one line a finding under its pass, bugs
first, each with what you changed or why you left it; then every clang-tidy finding you left, with why.
