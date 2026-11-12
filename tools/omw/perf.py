"""`omw profile [--offcpu] [--out=<dir>] [bench args]`: the renderer's CPU side under perf, over one place or a
suite, in the release build, which carries line tables and frame pointers so no second build explains a frame
nobody timed. **`--offcpu` sees the shape alone**: BPF walks frame pointers, which the driver lacks, so a wait inside
it is an address; read `blocked-libraries.txt` for which library the process waits in."""

import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import IO

from omw.build import Build
from omw.system import WINDOWS, Refusal, Switches, on_path, read_text, require, status, user_and_group

SAMPLES_A_SECOND = "5999"


def profile(build: Build, args: list[str]) -> int:
    if WINDOWS:
        raise Refusal("profile is perf's, and perf is Linux's")
    switches = Switches("profile", "the renderer's CPU side under perf, over the frames `bench` measures; the "
                                   "rest of the line goes to `bench`, `--views=` or `--suite=` among it")
    switches.add_argument("--offcpu", action="store_true", help="where the frame waits, and not where it works")
    switches.add_argument("--out", type=Path, default=build.dir / "perf", help="where the recording and its reports go")
    asked, extra = switches.parse_known_args(args)
    if any(arg.split("=", 1)[0] == "--json" for arg in extra):
        raise Refusal("profile reads the frames it measured off bench's record, so it names --json itself")
    require("perf", "it is what records the profile: perf")
    offcpu: bool = asked.offcpu
    out: Path = asked.out
    # One place unless told otherwise: an exterior and an interior do not spend their frame on the same thing.
    named = any(arg.split("=", 1)[0] in ("--views", "--suite") for arg in extra)
    place = [] if named else ["--views=seyda-neen-ship"]

    # On-CPU and off-CPU are two recordings, as `perf report` writes every event of a file to one page.
    out.mkdir(parents=True, exist_ok=True)
    data = out / ("blocked.data" if offcpu else "cpu.data")

    # perf's control fifo: `--delay=-1` starts the counters off and `bench` turns them on around the frames it
    # measures, so the recording is those frames alone.
    control = out / "control.fifo"
    control.unlink(missing_ok=True)
    os.mkfifo(control)
    try:
        # **perf's default ring buffer**, as `-m` past `perf_event_mlock_kb` is refused rather than clamped.
        record = ["perf", "record", "--delay=-1", f"--control=fifo:{control}", "-o", str(data)]
        if offcpu:
            # `dummy` never fires, so the samples are the BPF profiler's alone; waits of a millisecond and more, as
            # the default half second sees none of a frame's.
            record += ["-e", "dummy", "--off-cpu", "--off-cpu-thresh", "1", "--call-graph", "fp"]
        else:
            # **`task-clock` rather than `cycles`**, which a hybrid CPU counts as one event a core type, splitting
            # a thread that migrated; `task-clock` is one event, in nanoseconds.
            record += ["-e", "task-clock", "-F", SAMPLES_A_SECOND, "--call-graph", "fp"]

        bench = [str(part) for part in build.harness_line("bench", "--window=false", *place,
                                                          f"--perf-control={control}", f"--json={out / 'bench.json'}",
                                                          *extra)]
        with (out / "bench.txt").open("w", encoding="utf-8") as log:
            if offcpu:
                elevate = _elevation()
                code = _record_offcpu(build.dir, build.env, [*elevate, *record], bench, log)
                # Only what root wrote is owned by root.
                if elevate:
                    subprocess.run(["sudo", "chown", user_and_group(), str(data)], check=True)
            else:
                with subprocess.Popen([*record, "--", *bench], cwd=build.dir, env=build.env,
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding="utf-8",
                                      errors="replace") as recorded:
                    try:
                        _tee(recorded, log)
                        code = status(recorded.wait())
                    finally:
                        # perf takes the harness it started down with it.
                        if recorded.poll() is None:
                            recorded.terminate()
    finally:
        control.unlink(missing_ok=True)
    if code != 0:
        return code
    _report(data, out, "blocked" if offcpu else "cpu", offcpu)
    return 0


@dataclass(frozen=True)
class Measured:
    frames: int
    seconds: float


def measured_window(record: dict) -> Measured:
    """The frames `bench --json` measured and the wall seconds they took, over every place: the recording's window."""
    places = record.get("places", [])
    return Measured(sum(int(place["frames"]) for place in places),
                    sum(float(place["wallSeconds"]) for place in places))


def _perf_report(*args: str) -> str:
    return subprocess.run(["perf", "report", *args], capture_output=True, encoding="utf-8", errors="replace",
                          check=False).stdout


def _narrow(line: str, width: int = 86) -> str:
    """The symbol column, narrowed to a terminal's width: what identifies a C++ symbol is at its front."""
    return line if len(line) <= width else line[:width - 1] + "…"


def _symbol(line: str) -> str:
    return re.sub(r" {2,}.*$", "", line[line.index("] ") + 2:]) if "] " in line else ""


def _report(data: Path, out: Path, slug: str, blocked: bool) -> None:
    """The reports, every figure per second of what the run said it measured, which the recording is bounded to.
    **`--no-inline` throughout**: perf resolves the inline stack at a return address, which lands in whatever was
    inlined after the call and names the wrong thing."""
    measured = measured_window(json.loads(read_text(out / "bench.json")))
    frames, wall = measured.frames, measured.seconds

    common = ["-i", str(data), "--stdio", "--no-inline"]
    summary = _perf_report(*common, "-g", "none", "--sort", "dso")
    lost = next((line.split()[-1] for line in summary.splitlines() if "Total Lost Samples" in line), None)
    nanoseconds = next((line.split()[-1] for line in summary.splitlines() if "Event count" in line), None)

    # The last run's reports go first, so none is read as part of this profile.
    for stale in [*out.glob(f"{slug}-*.txt"), out / f"{slug}.folded", out / f"{slug}.svg"]:
        stale.unlink(missing_ok=True)

    reports = {
        "libraries": summary,
        "self": _perf_report(*common, "--no-children", "-g", "none", "--percent-limit", "0.3"),
        "total": _perf_report(*common, "--children", "-g", "none", "--sort", "symbol", "--percent-limit", "0.5"),
        "callers": _perf_report(*common, "--no-children", "-g", "graph,2,caller", "--percent-limit", "1"),
        # What `-g1` is in the release build for: a line resolves what a symbol cannot, such as libc's `memmove`.
        "lines": _perf_report(*common, "--no-children", "-g", "none", "--sort", "srcline", "--percent-limit", "0.5"),
    }
    for name, text in reports.items():
        (out / f"{slug}-{name}.txt").write_text(text)

    # A flame graph where something on this box folds a stack; the reports are the same data.
    folders = [("inferno-collapse-perf", "inferno-flamegraph"), ("stackcollapse-perf.pl", "flamegraph.pl")]
    folder = next((pair for pair in folders if on_path(pair[0])), None)
    if folder is not None:
        script = subprocess.run(["perf", "script", "-i", str(data), "--no-inline"], capture_output=True,
                                check=False).stdout
        folded = subprocess.run([folder[0]], input=script, capture_output=True, check=True).stdout
        (out / f"{slug}.folded").write_bytes(folded)
        graph = subprocess.run([folder[1], "--title", slug], input=folded, capture_output=True, check=True).stdout
        (out / f"{slug}.svg").write_bytes(graph)

    print(f"\nprofile: {frames:g} frames over {wall:.4f} s")
    if lost not in (None, "0"):
        print(f"  {lost} samples lost — the ring buffer overflowed at {SAMPLES_A_SECOND} samples a second")
    if nanoseconds is not None and wall > 0:
        share = float(nanoseconds) / 1e9 / wall
        if blocked:
            print(f"  blocked  {share:6.2f} thread-seconds of waiting per second")
        else:
            print(f"  on-CPU   {share:6.2f} cores busy, all the time")

    # **Pass-through frames come out**: one that spent nothing itself and cost what the frame above it cost is a
    # link in a chain, so the list starts where the cost first divides.
    print("\n  by total time — self and everything it called:")
    shown = 0
    above: float | None = None
    for line in reports["total"].splitlines():
        if not re.match(r"^ +[0-9]", line):
            continue
        fields = line.split()
        children, own = float(fields[0].rstrip("%")), float(fields[1].rstrip("%"))
        if above is None:
            above = children
        link = own == 0 and above - children < 0.5
        above = children
        if link:
            continue
        if shown >= 12:
            break
        shown += 1
        print(_narrow(f"    {fields[0]:>7} {fields[1]:>7}  {_symbol(line)}"))

    print("\n  by self time:")
    for line in [line for line in reports["self"].splitlines() if re.match(r"^ +[0-9]", line)][:12]:
        fields = line.split()
        print(_narrow(f"    {fields[0]:>7}  {fields[2]:<30} {_symbol(line)}"))

    print()
    for file in [*sorted(out.glob(f"{slug}*.txt")), out / f"{slug}.svg", out / "bench.txt"]:
        if file.exists():
            print(f"  {file}")
    if not (out / f"{slug}.svg").exists():
        print("  (no flame graph: install inferno)")


def _tee(process: subprocess.Popen, log: IO[str]) -> None:
    """What the run prints, to the terminal as it comes and to `bench.txt`."""
    assert process.stdout is not None
    for line in process.stdout:
        sys.stdout.write(line)
        log.write(line)


def _elevation() -> list[str]:
    """**Off-CPU sampling is BPF, which is privileged**: a `perf` carrying `cap_bpf` (`sudo setcap
    cap_perfmon,cap_bpf,cap_sys_ptrace+ep "$(command -v perf)"`) needs nothing more, one without runs under sudo.
    Asked of the binary each time, as an upgrade drops what was set on it."""
    perf = on_path("perf") or "perf"
    getcap = on_path("getcap")
    capabilities = "" if getcap is None else subprocess.run(
        [getcap, perf], capture_output=True, encoding="utf-8", errors="replace", check=False).stdout
    if "cap_bpf" in capabilities:
        return []
    print("profile: perf carries no cap_bpf — it runs under sudo, and the harness does not")
    subprocess.run(["sudo", "-v"], check=True)
    return ["sudo"]


def _record_offcpu(cwd: Path, env: dict[str, str], record: list[str], bench: list[str], log: IO[str]) -> int:
    """The harness started as the user, with a home, a Wayland socket and a GPU, and `record` attached to it.
    **Both owned to the end**, so `omw profile | head` takes the harness down; a `perf` that refused shows as its
    own status."""
    # The harness waits at its first place until perf opens the control fifo, so perf starts now.
    with (subprocess.Popen(bench, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           encoding="utf-8", errors="replace") as harness,
          subprocess.Popen([*record, "-p", str(harness.pid)]) as recorder):
        try:
            _tee(harness, log)
            code = status(harness.wait())
        finally:
            if harness.poll() is None:
                harness.kill()
        recorded = status(recorder.wait())
    if recorded != 0:
        print(f"profile: perf exited with {recorded}", file=sys.stderr)
        return recorded
    return code
