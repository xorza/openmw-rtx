"""`omw [flavour] noise --ab=<switch>[=<a>,<b>] [--still] [noise args]`: one harness switch's A/B on the
`noise` verb, both sides of each leg back to back, and the figures beside each other.

**The moving legs, and the still one only where it is asked for.** A run of `noise` warms each of
its independent draws for 128 frames before it measures: some 9,400 frames a place, five minutes for
the bounce suite on the RTX 4090 Laptop, which it keeps at 99%, so a second process beside it only
shares the card. What makes an A/B cheaper is fewer runs, and the moving legs are where the
denoiser's switches are mostly felt: the strafe and the walk are the default legs, each 150 units, as
`look.h` quotes them. **The still leg is not unmoved by them**: under the upscaler's jitter an edge
keeps a short history however long the eye stands, and the anti-firefly ring moved the bounce
suite's still frames by up to 0.09 of noise and 0.11 of bias. `--still` adds it, and `--strafe=N` or
`--walk=N` moves a leg's distance.

**A boolean switch by its name**: `--ab=antifirefly` runs `--antifirefly=true` and then
`--antifirefly=false`. A switch of values names its two: `--ab=bounce-reuse=own,spatiotemporal`.

**Every run keeps its pictures and its log**, under `--out` where it is given and a directory of its
own where it is not, one directory a side and a leg, because what an A/B finds is read in them."""

import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

from omw.build import Build
from omw.system import Refusal, Switches

# The distance `look.h` quotes the moving legs at.
DEFAULT_DISTANCE = "150"

# `RtxTool::judgeNoise`'s statuses: every frame as clean, or one noisier, which is an A/B's to read.
# Any other ended the run before it judged, and a crash after some places printed reads as a
# shorter suite.
JUDGED = (0, 1)

# One place's line of the harness's report, as `RtxTool::judgeNoise` prints it.
_PLACE = re.compile(
    r"^  (?P<place>\S+)\s+noise: frame mean (?P<mean>[\d.]+) p99 (?P<p99>\d+), (?P<frames>\d+) averaged mean "
    r"[\d.]+ p99 \d+ — (?:as clean|noisier); bias: frame (?P<bias>[\d.]+), \d+ averaged [\d.]+$")


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
    """One place's frame, as a leg's run measured it: its noise's mean and 99th percentile, and its
    bias against the converged reference."""
    mean: float
    p99: int
    bias: float


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
    switches.add_argument("--out", type=Path, help="where the runs go, a directory of their own if not given")
    asked, rest = switches.parse_known_args(args)
    legs = [Leg("still", ())] if asked.still else []
    legs += [Leg(f"{leg} {distance}", (f"--{leg}={distance}",))
             for leg, distance in (("strafe", asked.strafe), ("walk", asked.walk))]
    return Plan(_sides(asked.ab), tuple(legs), asked.out, tuple(rest))


def read_report(text: str) -> dict[str, Figures]:
    """Every place's figures in a run's report, by the place's name."""
    found: dict[str, Figures] = {}
    for line in text.splitlines():
        matched = _PLACE.match(line)
        if matched:
            found[matched["place"]] = Figures(float(matched["mean"]), int(matched["p99"]), float(matched["bias"]))
    return found


def table(leg: str, sides: tuple[Side, Side], first: dict[str, Figures], second: dict[str, Figures]) -> str:
    """One leg's places, each side's noise and bias beside the other's."""
    a, b = sides
    lines = [f"{leg}: {a.switch} against {b.switch}",
             f"  {'':<28} {'noise mean':>17} {'p99':>11} {'bias':>13}",
             f"  {'':<28} {a.label:>8} {b.label:>8} {a.label:>5} {b.label:>5} {a.label:>6} {b.label:>6}"]
    for place in [*first, *(place for place in second if place not in first)]:
        x, y = first.get(place), second.get(place)
        if x is None or y is None:
            lines.append(f"  {place:<28} measured on one side only")
            continue
        lines.append(f"  {place:<28} {x.mean:>8.2f} {y.mean:>8.2f} {x.p99:>5} {y.p99:>5} {x.bias:>6.2f} {y.bias:>6.2f}")
    return "\n".join(lines)


def ab(build: Build, args: list[str]) -> int:
    asked = plan(args)
    out = asked.out or Path(tempfile.mkdtemp(prefix="omw-noise-ab-"))
    out.mkdir(parents=True, exist_ok=True)

    tables: list[str] = []
    for leg in asked.legs:
        reports: list[dict[str, Figures]] = []
        for side in asked.sides:
            folder = out / f"{side.label}-{leg.label.replace(' ', '')}"
            log = folder.with_suffix(".log")
            print(f"noise: {leg.label}, {side.switch}", flush=True)
            with open(log, "w") as written:
                ended = build.harness("noise", side.switch, *leg.switches, *asked.rest, f"--out={folder}",
                                      stdout=written, stderr=written)
            report = read_report(log.read_text(errors="replace"))
            if ended.returncode not in JUDGED or not report:
                print(f"the run failed with status {ended.returncode}, see {log}", file=sys.stderr)
                return 1
            reports.append(report)
        tables.append(table(leg.label, asked.sides, reports[0], reports[1]))

    print("\n\n".join(tables))
    print(f"\nthe runs are in {out}")
    return 0
