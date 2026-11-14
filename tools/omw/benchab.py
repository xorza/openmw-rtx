"""`omw [flavour] bench --ab=<switch>[=<a>,<b>] [--legs=N] [bench args]`: one harness switch's timing A/B on
`bench`. A throwaway leg first, which compiles what a shader change made new and brings the card to its clock;
then N legs of each side in turn, a then b, so what the card drifts by over the run falls on both alike. Each
zone and each of the host's rows, by its median and its p99 over the legs, beside the spread of the legs' own
medians: a difference the spread does not exceed is no difference. Every leg keeps its record and its log, under
`--out` or a folder of its own."""

import contextlib
import json
import statistics
import sys
from dataclasses import dataclass
from pathlib import Path

from omw.build import Build
from omw.noise import Side, sides_of
from omw.system import Refusal, Switches, read_text, temporary_folder

# How many legs a side runs where the line names none: enough that the median of their medians is not one
# leg's luck, few enough that the run stays minutes.
DEFAULT_LEGS = 5


@dataclass(frozen=True)
class Plan:
    sides: tuple[Side, Side]
    legs: int
    out: Path | None
    rest: tuple[str, ...]


@dataclass(frozen=True)
class Row:
    """One zone or host row of one place over a side's legs: the median of the legs' medians and of their p99s,
    and how far the legs' medians spread."""
    median: float
    p99: float
    spread: float


def plan(args: list[str]) -> Plan:
    switches = Switches("bench", "one switch's timing A/B on `bench`; the rest of the line goes to every leg")
    switches.add_argument("--ab", required=True, help="the switch, or `<switch>=<a>,<b>` for two values")
    switches.add_argument("--legs", type=int, default=DEFAULT_LEGS, help="how many legs each side runs")
    switches.add_argument("--out", type=Path, help="where the legs go, a directory of their own if not given")
    asked, rest = switches.parse_known_args(args)
    if asked.legs < 2:
        raise Refusal(f"--legs={asked.legs} gives no spread to read a difference against: two or more")
    if any(arg == "--json" or arg.startswith("--json=") for arg in rest):
        raise Refusal("--json is the A/B's own, a record a leg")
    # Absolute, as each leg's `--json` names a file in it and the harness runs from the build directory.
    out = asked.out.resolve() if asked.out else None
    return Plan(sides_of(asked.ab), asked.legs, out, tuple(rest))


def read_rows(record: dict) -> dict[str, dict[str, tuple[float, float]]]:
    """Each place's zones and host rows in one leg's record (`RtxTool::writeJson`): by place, then by row, the
    median and the p99. A zone is named as the report names it, a host row with its `Ms` dropped and `cpu `
    before it."""
    places: dict[str, dict[str, tuple[float, float]]] = {}
    for place in record.get("places", []):
        rows: dict[str, tuple[float, float]] = {}
        for key, value in place.items():
            if key.endswith("Ms") and isinstance(value, dict) and "median" in value:
                rows["cpu " + key.removesuffix("Ms")] = (float(value["median"]), float(value["p99"]))
        for zone, value in place.get("gpuMs", {}).items():
            rows[zone] = (float(value["times"]["median"]), float(value["times"]["p99"]))
        places[place["view"]] = rows
    return places


def gather(legs: list[dict[str, dict[str, tuple[float, float]]]]) -> dict[str, dict[str, Row]]:
    """A side's legs, row by row: a row met on fewer legs than all is left out, as no median of it is whole."""
    gathered: dict[str, dict[str, Row]] = {}
    for place in legs[0]:
        rows: dict[str, Row] = {}
        for name in legs[0][place]:
            figures = [leg[place][name] for leg in legs if place in leg and name in leg[place]]
            if len(figures) != len(legs):
                continue
            medians = [median for median, _ in figures]
            rows[name] = Row(statistics.median(medians), statistics.median(p99 for _, p99 in figures),
                             max(medians) - min(medians))
        gathered[place] = rows
    return gathered


def table(sides: tuple[Side, Side], first: dict[str, dict[str, Row]], second: dict[str, dict[str, Row]]) -> str:
    """Each place's rows, the costliest first by the first side, each side's median, p99 and spread, and the
    median's change; a change no larger than either side's spread is marked as within it."""
    a, b = sides
    lines = [f"{a.switch} against {b.switch}, ms"]
    for place, rows in first.items():
        other = second.get(place, {})
        lines.append(f"  {place}")
        lines.append(f"    {'':<20} {a.label + ' median':>14} {b.label + ' median':>14} {'change':>8} "
                     f"{'spreads':>15} {a.label + ' p99':>11} {b.label + ' p99':>11}")
        for name in sorted(rows, key=lambda row: -rows[row].median):
            x, y = rows[name], other.get(name)
            if y is None:
                lines.append(f"    {name:<20} measured on one side only")
                continue
            change = y.median - x.median
            within = abs(change) <= max(x.spread, y.spread)
            lines.append(f"    {name:<20} {x.median:>14.3f} {y.median:>14.3f} {change:>+8.3f} "
                         f"{x.spread:>7.3f} {y.spread:>7.3f} {x.p99:>11.3f} {y.p99:>11.3f}"
                         f"{'  within the spread' if within else ''}")
    return "\n".join(lines)


def ab(build: Build, args: list[str]) -> int:
    asked = plan(args)
    build.build_harness()
    stated = contextlib.nullcontext(asked.out) if asked.out else temporary_folder("omw-bench-ab-")
    with stated as out:
        out.mkdir(parents=True, exist_ok=True)

        def leg(side: Side, name: str, record: Path | None) -> bool:
            log = out / f"{name}.log"
            print(f"bench: {name}, {side.switch}", flush=True)
            json_switch = [f"--json={record}"] if record else []
            with log.open("w", encoding="utf-8") as written:
                ended = build.harness("bench", side.switch, *json_switch, *asked.rest, stdout=written,
                                      stderr=written)
            if ended.returncode != 0 or (record is not None and not record.is_file()):
                print(f"the leg failed with status {ended.returncode}, see {log}", file=sys.stderr)
                return False
            return True

        if not leg(asked.sides[0], "warmup", None):
            return 1
        read: tuple[list, list] = ([], [])
        for index in range(1, asked.legs + 1):
            for at, side in enumerate(asked.sides):
                record = out / f"{side.label}-{index}.json"
                if not leg(side, f"{side.label}-{index}", record):
                    return 1
                read[at].append(read_rows(json.loads(read_text(record))))

        print(table(asked.sides, gather(read[0]), gather(read[1])))
        print(f"\nthe legs are in {out}")
        return 0
