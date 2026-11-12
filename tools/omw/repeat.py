"""`omw [flavour] repeat [--pairs=N] [bench args]`: runs of one binary walk `one-cell-walk` for six seconds and
must agree frame for frame (`bench --against`), so a run is a function of the binary alone. **Processes, not stops
of one**, which start from the world the last left; **a walk, not a still**, which hid both defects this caught; the
upscaler and the denoiser off, so what is compared is what a reconstruction is fed.

**A chain, every other run holding the queue behind the host**: each run is compared with the one before, so every
comparison is a held phase against a free one, and `--pairs=N` is N comparisons over N + 1 runs."""

import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from omw.build import Build
from omw.system import Refusal, Switches, read_text, temporary_folder

# `RtxTool::sDifferedStatus`: the run's one fault is a frame that differed from its reference.
DIFFERED_STATUS = 3

# **What `repeat` sets on every run**: what the walk is and how it is taken. Named twice, `bench` refuses the line,
# so each is refused here first.
WALK = {"--views": "one-cell-walk", "--seconds": "6"}
TAKEN = {"--window": "false", "--upscale": "off", "--filter": "false"}

# What would walk another way than `WALK` says, and what each run sets for itself.
WALK_SWITCHES = (*WALK, "--suite", "--frames")
RUN_SWITCHES = ("--hold", "--out", "--against")


@dataclass(frozen=True)
class Ran:
    """One run of the chain: its log, and the harness's status."""
    log: Path
    code: int


def _tail(log: Path) -> str:
    return "\n".join(read_text(log).splitlines()[-20:])


def repeat(build: Build, args: list[str]) -> int:
    switches = Switches("repeat", "two runs of one binary walk one place and must agree; the rest of the "
                                  "line goes to every run of `bench`, `--exposure=1 --pictures` to read one")
    switches.add_argument("--pairs", type=int, default=1, help="comparisons, over one more run than this")
    asked, extra = switches.parse_known_args(args)
    pairs: int = asked.pairs
    if pairs < 1:
        raise Refusal(f"--pairs={pairs} is not a count of one or more")
    for arg in extra:
        switch = arg.split("=", 1)[0]
        if switch in WALK_SWITCHES:
            raise Refusal(f"repeat walks `one-cell-walk` for six seconds, always, and {arg} would move it")
        if switch in TAKEN or switch in RUN_SWITCHES:
            raise Refusal(f"repeat sets {switch} itself, on every run")

    build.build_harness()
    with temporary_folder("omw-repeat-") as out:
        bench = [f"{switch}={value}" for switch, value in (WALK | TAKEN).items()] + extra

        def run(index: int) -> Ran:
            log = out / f"{index}.log"
            held = ["--hold"] if index % 2 else []
            against = [f"--against={out / str(index - 1)}"] if index else []
            # **Through a pipe and not into the log**: the crash monitor shares the output and ends after the
            # harness, and Windows refuses to remove a file still open.
            ended = build.harness("bench", *bench, *held, f"--out={out / str(index)}", *against,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            log.write_bytes(ended.stdout)
            return Ran(log, ended.returncode)

        first = run(0)
        if first.code != 0:
            print(f"the run itself failed, see {first.log}:\n{_tail(first.log)}", file=sys.stderr)
            return 1

        status = 0
        for pair in range(1, pairs + 1):
            second = run(pair)
            if second.code == 0:
                print(f"pair {pair} of {pairs}: identical")
            elif second.code == DIFFERED_STATUS:
                # Decided by the status alone; the report's comparison is only shown, from where it opens.
                status = 1
                lines = read_text(second.log).splitlines()
                against = next((i for i, line in enumerate(lines) if line.startswith("against ")), None)
                print(f"pair {pair} of {pairs}: NOT repeatable", file=sys.stderr)
                print(_tail(second.log) if against is None else "\n".join(lines[against:]), file=sys.stderr)
            else:
                print(f"the run itself failed, see {second.log}:\n{_tail(second.log)}", file=sys.stderr)
                return 1

        if status == 0:
            print(f"repeat: {pairs} pair(s), identical over every one")
            shutil.rmtree(out)
        else:
            print(f"the runs are in {out}", file=sys.stderr)
        return status
