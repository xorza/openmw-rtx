# Reviewing an upstream merge

What the review job's agent follows, read from `master` and never from the merge, whose `.github/` is
upstream's to change. It is the maintainer's own review of a change, scoped to what a merge can decide, by the
two passes of `passes.md`.

## Scope

- **The resolutions**: every file in `.upstream-merge/edited.txt`, which the merge's resolver changed past what git
  merged on its own, read against its sides in `.upstream-merge/stages/` (`.base`, `.fork`, `.upstream`).
- **What the merge adds** (`.upstream-merge/added.txt`) where it lands in code the fork removed or replaced: a
  file there goes, by its path in `.upstream-merge/removed.txt`.
- **Nothing else.** What upstream changed and git merged cleanly is upstream's and is not reviewed: `AGENTS.md`
  says upstream code changes only on the maintainer's yes, and a finding outside the scope goes into the report,
  unfixed. The pull request's CI builds, tests and lints the merge, clang-tidy included, and what it finds is
  answered there.

Fix every finding you can defend, in the working tree. A redesign that reaches past the merge: describe it in
the report and change nothing.

## What a merge adds to the passes

The intent is per resolution: state in one sentence what each set out to keep, and judge it against that.

- **Approach.** Both sides survive where both have a reason: upstream's change to a line and the fork's reason for
  having moved it. A side taken wholesale is a patch over a conflict, not its resolution, as the passes' other
  patch signs are.
- **Correctness.** Where upstream changed a rule the fork keeps a copy of for its renderer, the copy follows
  (`AGENTS.md` says where the ray tracer's side lives).
- **Tests.** A test you write for a bug you found runs in the pull request's CI, as you cannot run it.
- **Cleanup**, inside the scope only: dead code the merge left counts among the passes' simplicity.

## Report

Write `UPSTREAM-REVIEW.md` at the root of the checkout, for the human who reads the pull request: the verdict on
the resolutions first (structural or a patch, correct or not), then one line a finding under its pass, bugs
first, each with what you changed or why you left it.
