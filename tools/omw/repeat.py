"""`omw [flavour] repeat [--pairs=N] [bench args]`: two runs of one binary walk one place and must agree.

**Two processes and not two stops of one.** A second stop starts from the world the first one left,
so the two cannot be compared frame for frame. What this asks is whether a run of the binary is a
function of the binary, and only a second run of it answers that. A walk and not a still, because a
still passed through both of the defects this catches: a camera that stands still was exactly
reproducible while `osg::FrameStamp`'s reference time aged OpenMW's caches by the wall, and again
while MyGUI aged the hit overlay by one.

**The upscaler and the denoiser are off**, so what is walked is what a reconstruction is fed, which
is where every defect this has caught showed itself. The exposure stays measured; add `--exposure=1`
to read a difference in the picture, since a measured exposure couples every pixel to every other
and every frame to the one before.

**Every column is the gate.** The second run is compared by `bench --against`, which names the
frames whose picture moved and the parts of the scene that did, and fails on either. One pair gates
and ten read. Both legs' hashes are kept beside the logs of a pair that differed.

**Every other run holds the queue behind the host**, as far as `check` holds it. Two runs of one
binary keep the same phase between the host and the device, so a frame that read the device's clock
could repeat exactly and still be a function of the wall; `Rtx::Timeline` says why no frame reads it
now. Held, the device trails by half a frame, which is the other phase a run can have.

**A chain and not separate pairs**: each run is compared with the one before it, and the two always
differ in the hold, so every comparison is a held run against a free one, as a pair was. `--pairs=N`
is N comparisons, over N + 1 runs rather than 2N, and each run past the first is a fresh draw of
its phase that two comparisons read."""

import shutil
import sys
import tempfile
from pathlib import Path

from omw.build import Build

# `RtxTool::sDifferedStatus`: the run's one fault is a frame that differed from its reference.
DIFFERED_STATUS = 3


def _tail(log: Path) -> str:
    return "\n".join(log.read_text(errors="replace").splitlines()[-20:])


def repeat(build: Build, args: list[str]) -> int:
    pairs = 1
    place: list[str] = []
    length: list[str] = []
    extra: list[str] = []
    for arg in args:
        if arg.startswith("--pairs="):
            spelled = arg.split("=", 1)[1]
            if not spelled.isdigit() or int(spelled) < 1:
                print(f"--pairs={spelled} is not a count of one or more", file=sys.stderr)
                return 2
            pairs = int(spelled)
        elif arg.startswith(("--views=", "--suite=")):
            place.append(arg)
        elif arg.startswith(("--seconds=", "--frames=")):
            length.append(arg)
        else:
            extra.append(arg)
    place = place or ["--views=one-cell-walk"]
    length = length or ["--seconds=6"]

    build.build(["openmw-rtxtool"])
    out = Path(tempfile.mkdtemp(prefix="omw-repeat-"))
    bench = [build.binary("openmw-rtxtool"), "bench", *place, *length, "--window=false", "--upscale=off",
             "--filter=false", "--validation=off", *extra]

    def run(index: int) -> tuple[Path, int]:
        log = out / f"{index}.log"
        held = ["--hold"] if index % 2 else []
        against = [f"--against={out / f'{index - 1}.csv'}"] if index else []
        with open(log, "w") as written:
            ended = build.run_here([*bench, *held, f"--hashes={out / f'{index}.csv'}", *against], stdout=written,
                                   stderr=written)
        return log, ended.returncode

    first, code = run(0)
    if code != 0:
        print(f"the run itself failed, see {first}:\n{_tail(first)}", file=sys.stderr)
        return 1

    status = 0
    for pair in range(1, pairs + 1):
        second, code = run(pair)
        lines = second.read_text(errors="replace").splitlines()
        against = next((i for i, line in enumerate(lines) if line.startswith("against ")), None)
        if code == 0:
            print(f"pair {pair} of {pairs}: identical")
        elif code == DIFFERED_STATUS and against is not None:
            status = 1
            print(f"pair {pair} of {pairs}: NOT repeatable", file=sys.stderr)
            print("\n".join(lines[against:]), file=sys.stderr)
        else:
            print(f"the run itself failed, see {second}:\n{_tail(second)}", file=sys.stderr)
            return 1

    if status == 0:
        print(f"repeat: {pairs} pair(s), identical over every one")
        shutil.rmtree(out)
    else:
        print(f"the runs are in {out}", file=sys.stderr)
    return status
