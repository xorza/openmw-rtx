# Proposal: one spelling for the tree, and the content's memory

Every item but the close has landed or been declined. Their decisions stand in `docs/architecture.md`, `AGENTS.md` and
`.notes/UPSTREAM.md`; what each item did, changed or declined is in this file's history (`git log -p` on it). What is
left is item 18, below, and what its tours have shown so far. Written for `/execute-plan`; this file is deleted when
the close lands.

## Where it runs

- **The desk**, where the run works and builds. In use during the run, so its frame times are not read; memory and
  faults are.
- **The handheld**, through `./omw release remote <handheld> <verb>`, which builds here and stages the release there.
  Steam and the desktop held 1.2–1.6 GB of its memory as a GPU's through every Repack tour of this round, which left
  the video heap a budget of about 4.8 GB, against 6.1 GB when the baseline was flown.
- **The Windows machine**, through the same verb, which builds the commit under test in its own clone and runs in its
  logged-on session.

The baseline is item 8's: two flights of each content on each machine, under `$HOME/Videos/openmw-rtx/baseline`, held
by each leg's worst frame against the worse of the two plus their spread.

## 18. Close

`./omw gate`; `./omw release tour` of the three contents on the three machines, held against the baseline. They pass
where:

1. no tour meets a fault, an outrun among them;
2. no leg's worst frame is longer than that leg's at the baseline by more than the baseline's own spread;
3. each of the baseline's ten longest frames on the handheld is gone, or stands proposed with its profile.

A condition that fails is worked, not logged: its cause is found and fixed, or what owed it is declined with what was
found. Then `.notes/ISSUES.md` is rewritten from the tours, the tours' figures go into a dated research note
(`docs/research/2026-11-18-closing-tours.md`, which `docs/architecture.md` already cites), and this file is deleted.

### What the tours show so far

| machine | content | commit | faults | legs' p99 median, against the baseline's |
|---|---|---|---|---|
| desk | all three | `9f41093017` | none | not read |
| handheld | vanilla | `9f41093017` | none | 18.4 ms, against 19.6 and 18.8 |
| handheld | PBR | `9f41093017` | the stand-in at Seyda Neen | 21.6 ms, against 22.2 and 22.5; the baseline's outruns gone |
| handheld | Repack | `16a5de2beb` | a mesh refused | 51.5 ms, against 26.2 and 26.0 |
| handheld | Repack | `37e2d756d7` | a texture and a mesh refused | 73.2 ms, with the defragmentation since reverted |
| Windows | PBR | `87b6f3a4d8` | 3 outruns | 28.6 ms, against 29.0 and 29.0 |

### Left to do

- **The handheld's Repack at `c8664cd001`**, flying now: the device's reserve below the caches' alone, the
  defragmentation reverted. It decides what the issues say of the Repack's frames.
- **Windows' vanilla and Repack**, flying now, built at `87b6f3a4d8`, the heap no longer walked once a second. The
  Repack at `9f41093017` was stopped past four hours at stop 41, every frame waiting on that walk.
- **The research note, the issues rewritten, the gate, CI green**, then this file deleted.

### Open, for the issues rewrite

- **The handheld's Repack frames**: the legs' p99 median rose from 26 ms at the baseline to 51 ms, its room legs
  spiking to 30–40 ms a few frames a stop with nothing arriving, in 62 of 99 rooms. A profile of the room legs puts the
  time in the host waiting for the device, while no timed zone of the device passes 8 ms. The device holds 5.8 GB there
  against a budget of 4.8 GB, the desktop's share of the memory taken, where the baseline held 5.0 GB against 6.1 GB.
- **The handheld's Repack refuses three 64 MiB structure blocks** each tour, with 887 MiB free in the texture pool's
  blocks and 2.5 GiB of resting meshes beside them. Freeing the oldest resting meshes and moving the textures together
  were each built and declined.
- **Windows' private memory past its heaps**: 11 GiB in some 950 regions of up to 68.6 MiB at the PBR tour's stop 90,
  beside 2.8 GiB in the heaps' regions (`Platform::Process::privateMemoryHeld`). What allocates them is not named.
- **Windows' PBR outran three cells**, where its baseline outran none.
- **The handheld's PBR draws the stand-in for good** once, at Seyda Neen, as every tour before it did.
