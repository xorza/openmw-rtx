# The review's two passes

What every review of a change in this repository judges, by an agent or by a person. The upstream merge's
(`.github/upstream-merge/review.md`) says what it covers, what it does with a finding, and how it reports; this
file says only what a finding is.

State the change's intent in one sentence and judge against it.

## Pass one: design

- **Approach.** The cause is fixed at the layer that owns the invariant, not a symptom patched. Patch signs: a
  special case, a flag threaded down, a retry or sleep hiding a race, a fallback hiding a wrong value, state synced
  by hand, a fix at one site while other sites keep the defect.
- **Correctness.** Hunt bugs in every changed line: inverted conditions, off-by-ones, swapped arguments, unhandled
  cases, lost errors, leaks, races, boundary inputs, error paths, partial failure. Check every caller and every
  list of the changed thing: match arms, registries, serialization, config, docs.
- **Robustness.** The data structure or ownership change that makes the defect class impossible or loud.
- **Tests.** Every new or modified behavior has a test with exact values through the contract: boundaries, error
  paths, each parameter shown to matter. Each bug found needs a test that fails without its fix. Weak tests are
  findings too: happy path only, a range instead of a value, internals a refactor would break.

## Pass two: cleanup

On what pass one left:

- **Reuse.** Existing helpers and types replace new code that duplicates them; overlapping abstractions and types
  merge.
- **Granularity.** Each type and function holds one responsibility, and each responsibility lives in one. Merge
  the fragments: types always built, passed or changed together; one-caller functions that read simpler inline;
  chains of tiny functions that only hand off. Split what changes for different reasons. Many tiny pieces are as
  wrong as one that does everything.
- **Simplicity.** Simplification, dead code, `AGENTS.md`'s conventions, comments that narrate rather than say why.
