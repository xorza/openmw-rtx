"""`omw [flavour] noise --ab=<switch>[=<a>,<b>] [--still] [noise args]`: one harness switch's A/B on the
`noise` verb, both sides of each leg in one run, and the figures beside each other.

**One run a leg, both sides in it** (`noise --versus`): the second side draws what the first drew,
and where the switch is one only the filters read, its reference and its bar are the first side's,
which half of a run is. A run of `noise` warms each of its independent draws for 128 frames before
it measures: some 9,400 frames a place, five minutes for the bounce suite on the RTX 4090 Laptop,
which it keeps at 99%, so a second process beside it only shares the card.

**The moving legs, and the still one only where it is asked for.** What makes an A/B cheaper is
fewer runs, and the moving legs are where the denoiser's switches are mostly felt: the strafe and the
walk are the default legs, each 150 units, as `look.h` quotes them, and a distance of nought leaves
one out. An investigation narrows to the place and the leg that show the effect first, with
`--views=` and `--strafe=0 --walk=0 --still`, and the whole suite is its verdict. **The still leg is
not unmoved by them**: under the upscaler's jitter an edge keeps a short history however long the
eye stands, and the anti-firefly ring moved the bounce suite's still frames by up to 0.09 of noise
and 0.11 of bias. `--still` adds it, and `--strafe=N` or `--walk=N` moves a leg's distance.
`--cut=N` adds the frame `N` frames after a cut, standing, once for each `N` named: the first frames
after a door, where the fireflies were reported.

**A boolean switch by its name**: `--ab=antifirefly` runs `--antifirefly=true` and then
`--antifirefly=false`. A switch of values names its two: `--ab=bounce-reuse=own,spatiotemporal`.

**Every run keeps its pictures and its log**, under `--out` where it is given and a directory of its
own where it is not, one directory a leg, because what an A/B finds is read in them."""

import json
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

from omw.build import Build
from omw.system import Refusal, Switches, read_text

# The distance `look.h` quotes the moving legs at.
DEFAULT_DISTANCE = "150"

# `RtxTool::judgeNoise`'s statuses: every frame as clean, or one noisier, which is an A/B's to read.
# Any other ended the run before it judged, and a crash after some places printed reads as a
# shorter suite.
JUDGED = (0, 1)

# What `noise` names the record it writes beside its pictures (`RtxTool::sNoiseRecord`).
RECORD = "noise.json"


@dataclass(frozen=True)
class Side:
    label: str
    switch: str


@dataclass(frozen=True)
class Leg:
    label: str
    switches: tuple[str, ...]


@dataclass(frozen=True)
class Plan:
    sides: tuple[Side, Side]
    legs: tuple[Leg, ...]
    out: Path | None
    rest: tuple[str, ...]


@dataclass(frozen=True)
class Figures:
    """One place's frame, as a leg's run measured it: its noise's mean and 99th percentile, its bias
    against the converged reference, and its fireflies in a thousand pixels."""
    mean: float
    p99: float
    bias: float
    fireflies: float


def wants_ab(args: list[str]) -> bool:
    return any(arg == "--ab" or arg.startswith("--ab=") for arg in args)


def _sides(asked: str) -> tuple[Side, Side]:
    name, _, values = asked.partition("=")
    if not name or name.startswith("-"):
        raise Refusal(f"--ab={asked} names no switch: `--ab=antifirefly` or `--ab=bounce-reuse=own,temporal`")
    if not values:
        return Side("on", f"--{name}=true"), Side("off", f"--{name}=false")
    pair = values.split(",")
    if len(pair) != 2 or not all(pair):
        raise Refusal(f"--ab={asked} names {len(pair)} values of {name}, and an A/B is two")
    return Side(pair[0], f"--{name}={pair[0]}"), Side(pair[1], f"--{name}={pair[1]}")


def plan(args: list[str]) -> Plan:
    """What the line asks: the two sides, the legs in the order they run, where the runs go, and
    what every run is handed beside them."""
    switches = Switches("noise", "one switch's A/B on `noise`; the rest of the line goes to every run")
    switches.add_argument("--ab", required=True, help="the switch, or `<switch>=<a>,<b>` for two values")
    switches.add_argument("--still", action="store_true", help="the still leg as well")
    switches.add_argument("--strafe", default=DEFAULT_DISTANCE, help="how far the strafed leg flies in")
    switches.add_argument("--walk", default=DEFAULT_DISTANCE, help="how far the walked leg walks in")
    switches.add_argument("--cut", type=int, action="append", default=[],
                          help="a leg of the frame this many frames after a cut; named again, one more")
    switches.add_argument("--out", type=Path, help="where the runs go, a directory of their own if not given")
    asked, rest = switches.parse_known_args(args)
    if any(frames < 1 for frames in asked.cut):
        raise Refusal(f"--cut={min(asked.cut)} is not a frame after the cut: one or more")
    legs = [Leg("still", ())] if asked.still else []
    legs += [Leg(f"cut {frames}", (f"--cut={frames}",)) for frames in asked.cut]
    legs += [Leg(f"{leg} {distance}", (f"--{leg}={distance}",))
             for leg, distance in (("strafe", asked.strafe), ("walk", asked.walk)) if not _nought(distance)]
    if not legs:
        raise Refusal("no leg is left to run: name --still, --cut=N, or a distance that is not nought")
    return Plan(_sides(asked.ab), tuple(legs), asked.out, tuple(rest))


def _nought(distance: str) -> bool:
    """Whether a leg's distance leaves it out. What is no number is the harness's to refuse."""
    try:
        return float(distance) == 0.0
    except ValueError:
        return False


def read_record(record: dict) -> tuple[dict[str, Figures], ...]:
    """Every side's places in a run's record (`RtxTool::writeNoiseRecord`), each by its place's name,
    the first side first."""
    return tuple({place["place"]: Figures(float(place["mean"]), float(place["p99"]), float(place["bias"]),
                                          float(place["fireflies"])) for place in side}
                 for side in record.get("sides", []))


def table(leg: str, sides: tuple[Side, Side], first: dict[str, Figures], second: dict[str, Figures]) -> str:
    """One leg's places, each side's noise and bias beside the other's. **Every column as wide as the
    longer label**, because a side is named by what the line gave it: `spatiotemporal` is fourteen."""
    a, b = sides
    label = max(len(a.label), len(b.label))
    mean, p99, bias, fly = max(label, 8), max(label, 5), max(label, 6), max(label, 9)
    lines = [f"{leg}: {a.switch} against {b.switch}",
             (f"  {'':<28} {'noise mean':>{2 * mean + 1}} {'p99':>{2 * p99 + 1}} {'bias':>{2 * bias + 1}} "
              f"{'fireflies in 1000':>{2 * fly + 1}}"),
             (f"  {'':<28} {a.label:>{mean}} {b.label:>{mean}} {a.label:>{p99}} {b.label:>{p99}} {a.label:>{bias}} "
              f"{b.label:>{bias}} {a.label:>{fly}} {b.label:>{fly}}")]
    for place in [*first, *(place for place in second if place not in first)]:
        x, y = first.get(place), second.get(place)
        if x is None or y is None:
            lines.append(f"  {place:<28} measured on one side only")
            continue
        lines.append(f"  {place:<28} {x.mean:>{mean}.2f} {y.mean:>{mean}.2f} {x.p99:>{p99}.2f} {y.p99:>{p99}.2f} "
                     f"{x.bias:>{bias}.2f} {y.bias:>{bias}.2f} {x.fireflies:>{fly}.2f} {y.fireflies:>{fly}.2f}")
    return "\n".join(lines)


def leg_log(folder: Path) -> Path:
    """The log beside a leg's folder: its whole name and `.log`, which `with_suffix` is not for a
    decimal distance's — `walk1.5` became `walk1.log`, the log of a leg of one."""
    return folder.parent / (folder.name + ".log")


def ab(build: Build, args: list[str]) -> int:
    asked = plan(args)
    out = asked.out or Path(tempfile.mkdtemp(prefix="omw-noise-ab-"))
    out.mkdir(parents=True, exist_ok=True)

    first, second = asked.sides
    tables: list[str] = []
    for leg in asked.legs:
        folder = out / leg.label.replace(" ", "")
        log = leg_log(folder)
        print(f"noise: {leg.label}, {first.switch} against {second.switch}", flush=True)
        with open(log, "w", encoding="utf-8") as written:
            ended = build.harness("noise", first.switch, f"--versus={second.switch.removeprefix('--')}",
                                  *leg.switches, *asked.rest, f"--out={folder}", stdout=written, stderr=written)
        record = folder / RECORD
        sides = read_record(json.loads(read_text(record))) if record.is_file() else ()
        if ended.returncode not in JUDGED or len(sides) != 2 or not all(sides):
            print(f"the run failed with status {ended.returncode}, see {log}", file=sys.stderr)
            return 1
        tables.append(table(leg.label, asked.sides, *sides))

    print("\n\n".join(tables))
    print(f"\nthe runs are in {out}")
    return 0
