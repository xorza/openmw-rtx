"""`omw [flavour] noise --ab=<switch>[=<a>,<b>] [--still] [noise args]`: one harness switch's A/B on `noise`, the
figures side by side. **Both sides in one run a leg** (`noise --versus`), so the second draws what the first drew;
a second process beside it would only share the card it keeps busy.

The strafe and the walk legs by default, 150 units each as `look.h` quotes them, a distance of nought leaving one
out; `--still` adds the still leg, which the upscaler's jitter still moves, and each `--cut=N` the frame N frames
after a cut. `--ab=antifirefly` runs true then false, `--ab=noise=blue-noise,white-hash` two values. Every leg keeps
its pictures and its log, under `--out` or a folder of its own."""

import contextlib
import json
import sys
from dataclasses import dataclass
from pathlib import Path

from omw.build import Build
from omw.system import Refusal, Switches, read_text, temporary_folder

# The distance `look.h` quotes the moving legs at.
DEFAULT_DISTANCE = "150"

# `RtxTool::judgeNoise`'s statuses: every frame as clean, or one noisier, which is an A/B's to read. Any other
# ended the run before it judged.
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
    against the converged reference, its fireflies in a thousand pixels, and the noise's mean on its noisiest
    tile (`RtxTool::worstTile`)."""
    mean: float
    p99: float
    bias: float
    fireflies: float
    tile: float


def wants_ab(args: list[str]) -> bool:
    return any(arg == "--ab" or arg.startswith("--ab=") for arg in args)


def _sides(asked: str) -> tuple[Side, Side]:
    name, _, values = asked.partition("=")
    if not name or name.startswith("-"):
        raise Refusal(f"--ab={asked} names no switch: `--ab=antifirefly` or `--ab=noise=blue-noise,white-hash`")
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
                                          float(place["fireflies"]), float(place["tileMean"])) for place in side}
                 for side in record.get("sides", []))


def table(leg: str, sides: tuple[Side, Side], first: dict[str, Figures], second: dict[str, Figures]) -> str:
    """One leg's places, each side's noise and bias beside the other's, every column as wide as the longer label,
    which the line names."""
    a, b = sides
    label = max(len(a.label), len(b.label))
    mean, p99, bias, fly, tile = max(label, 8), max(label, 5), max(label, 6), max(label, 9), max(label, 6)
    lines = [f"{leg}: {a.switch} against {b.switch}",
             (f"  {'':<28} {'noise mean':>{2 * mean + 1}} {'p99':>{2 * p99 + 1}} {'bias':>{2 * bias + 1}} "
              f"{'fireflies in 1000':>{2 * fly + 1}} {'worst tile':>{2 * tile + 1}}"),
             (f"  {'':<28} {a.label:>{mean}} {b.label:>{mean}} {a.label:>{p99}} {b.label:>{p99}} {a.label:>{bias}} "
              f"{b.label:>{bias}} {a.label:>{fly}} {b.label:>{fly}} {a.label:>{tile}} {b.label:>{tile}}")]
    for place in [*first, *(place for place in second if place not in first)]:
        x, y = first.get(place), second.get(place)
        if x is None or y is None:
            lines.append(f"  {place:<28} measured on one side only")
            continue
        lines.append(f"  {place:<28} {x.mean:>{mean}.2f} {y.mean:>{mean}.2f} {x.p99:>{p99}.2f} {y.p99:>{p99}.2f} "
                     f"{x.bias:>{bias}.2f} {y.bias:>{bias}.2f} {x.fireflies:>{fly}.2f} {y.fireflies:>{fly}.2f} "
                     f"{x.tile:>{tile}.2f} {y.tile:>{tile}.2f}")
    return "\n".join(lines)


def leg_log(folder: Path) -> Path:
    """The log beside a leg's folder: its whole name and `.log`, where `with_suffix` turned `walk1.5` into
    `walk1.log`."""
    return folder.parent / (folder.name + ".log")


def ab(build: Build, args: list[str]) -> int:
    asked = plan(args)
    build.build_harness()
    stated = contextlib.nullcontext(asked.out) if asked.out else temporary_folder("omw-noise-ab-")
    with stated as out:
        out.mkdir(parents=True, exist_ok=True)

        first, second = asked.sides
        tables: list[str] = []
        for leg in asked.legs:
            folder = out / leg.label.replace(" ", "")
            log = leg_log(folder)
            print(f"noise: {leg.label}, {first.switch} against {second.switch}", flush=True)
            with log.open("w", encoding="utf-8") as written:
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
