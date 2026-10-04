# Redesign for the open issues

This plan covers the open issues in `.notes/ISSUES.md`, investigated on 2026-10-04 on `master`
with `restir-gi` merged into the working tree. Each section gives the evidence, the cause, the
redesign and the steps. The last section gives the order of the work.

Issue 4 starts with measurements; the design depends on what they show.

| # | Issue | Cause found | Fix | Approval |
|---|-------|-------------|-----|----------|
| 4 | Host rows move between runs | Not proven yet. Data layout for the whole process is the main suspect | Experiments first, then the layout fix they show | Decided |

---

## 4. A measured run's host rows move between runs of one build

**Decided:** a session runs the experiments in the background and ends its turn while they run.

### Evidence

At `one-cell-walk`, six legs held to the performance cores at a steady clock read walk medians
of 1.02 to 1.53 ms, and the frame thread's cache misses per thousand instructions moved with
them, 3.14 to 4.75. All host rows move together, not only the walk.

### What the code shows

- The walk (`SceneExtractor`, `WorldMirror`) follows OSG nodes each frame. Those nodes are
  allocated when a cell loads, on the loader threads. glibc gives each thread an arena, and which
  arena a thread gets depends on timing. So the heap layout of the graph changes from run to run.
- The identity tables (`boost::unordered_flat_map` keyed by node address, `ByAddress`) put their
  entries by the address's hash. So the probe order changes with the layout.
- The kernel is booted with `transparent_hugepage=madvise`, and nothing in the tree calls
  `madvise(MADV_HUGEPAGE)`. So the heap is in 4 KiB pages, and their physical placement, which
  sets which L2 and L3 sets the data uses, changes on each run.

All three can move every row together. The data does not yet say which one does. Measurement bias
of this kind is well known (Mytkowicz et al. 2009, *Producing Wrong Data Without Doing Anything
Obviously Wrong!*). So the first step is experiments, not a design.

### Experiments

Each experiment runs six legs of `./omw release bench --views=one-cell-walk`. Start each in the
background on a quiet desktop and end the turn, as `AGENTS.md` says. Read the walk median, the p99,
and the miss rate per leg. The spread to beat is 1.02 to 1.53 ms.

| | Change for the run | If the spread shrinks, the cause is |
|---|---|---|
| E1 | `setarch -R` (no ASLR) | virtual layout and hash order |
| E2 | `GLIBC_TUNABLES=glibc.malloc.arena_max=1` | the loader threads' arenas |
| E3 | `GLIBC_TUNABLES=glibc.malloc.hugetlb=1` (THP for malloc) | physical page placement |
| E4 | E2 and E3 together | both |

For each leg, also record the frame thread's dTLB and L2 misses, beside the miss rate that the
harness's thread counters (`instruments/threadcounters.hpp`) gave for the issue. That shows which
level of the memory hierarchy moves.

### Designs, by result

- **E3 wins (page placement):** The renderer's host side asks for huge pages for the memory the
  walk reads: the mirror's tables and the `SceneDesc` rows, with `madvise(MADV_HUGEPAGE)` on
  allocations it owns, behind `Platform::` for the POSIX and Win32 split. The OSG graph is
  upstream's and stays as it is. The harness also reports the THP state in its run header, so that
  two runs that differ say so.
- **E2 wins (arenas):** The walk must not follow memory that a race placed. Move the per-frame
  reads to what the mirror owns: the frozen runs (`mFrozen`, `RunBuffer`) already flatten static
  subtrees. Extend that to the live parts the walk reads every frame, with contiguous storage and
  indices, not node pointers. This is a large change, and it gets its own plan after E2.
- **E1 wins (hash order):** Key the identity tables by a stable identity
  (`SceneUtil::StableIdentity`, which the walk already reads), not by address.
- **Nothing shrinks it:** The cause is outside the process (the scheduler, the compositor,
  firmware). Record that in the issue, and give the measured runs a check of the core and
  frequency state per leg.

---

## Order of work

1. **Issue 4's experiments**, then the design they select.

Each issue is deleted from `.notes/ISSUES.md` when its fix is verified. A problem found on the way
goes to the log, not into the issue's change.
