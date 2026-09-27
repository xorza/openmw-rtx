"""`omw profile [--view=<place>] [--offcpu] [--dwarf] [--tui] [bench args]`: the renderer's CPU side
under perf, over one place or a whole suite, in the release build.

**What `--offcpu` can and cannot see.** BPF collects its stacks by frame pointer and the graphics
driver has none, so a wait that starts inside `vkWaitForFences` is recorded as an address rather than
as the frame that asked for it, and `--dwarf` cannot help: a BPF-collected stack cannot be unwound
any other way. What the mode does establish is the shape — which library the process waits in, and
whether a wait passes through this fork's own code at all. Read the by-library table; the total above
it is dominated by driver worker threads parked for the length of the run.

**It profiles the build `omw release` measures, and does not have one of its own.** A profile is only
as good as its call graph, and a stock Release build has neither line numbers nor frame pointers — so
the release flavour carries `-g1 -fno-omit-frame-pointer` instead, which costs less than the
run-to-run spread. A second build directory would explain a frame nobody timed."""

import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import IO

from omw.build import Build
from omw.system import WINDOWS, Refusal, require


def profile(build: Build, args: list[str]) -> int:
    if WINDOWS:
        raise Refusal("profile is perf's, and perf is Linux's")
    require("perf", "it is what records the profile: perf")

    mode, unwind, frequency, tui = "cpu", "fp", "5999", False
    out = build.dir / "perf"
    place: list[str] = []
    extra: list[str] = []
    for arg in args:
        if arg == "--offcpu":
            mode = "offcpu"
        elif arg == "--dwarf":
            unwind = "dwarf"
        elif arg.startswith("--freq="):
            frequency = arg.split("=", 1)[1]
        elif arg == "--tui":
            tui = True
        elif arg.startswith("--out="):
            out = Path(arg.split("=", 1)[1])
        elif arg.startswith("--view="):
            place.append(f"--views={arg.split('=', 1)[1]}")
        elif arg.startswith(("--views=", "--suite=")):
            place.append(arg)
        else:
            extra.append(arg)
    # One place unless told otherwise. A profile that averaged an exterior and an interior would
    # describe neither: the two do not spend their frame on the same thing.
    place = place or ["--views=seyda-neen-ship"]

    # One file per question. On-CPU and off-CPU are two recordings rather than two events in one,
    # because `perf report` writes every event in a file to the same page and cannot be asked for one.
    out.mkdir(parents=True, exist_ok=True)
    data = out / ("blocked.data" if mode == "offcpu" else "cpu.data")

    if tui:
        if not data.is_file():
            raise Refusal(f"no recording at {data}: run `omw profile` without --tui first")
        return subprocess.run(["perf", "report", "-i", str(data), "--no-inline"]).returncode

    build.build(["openmw-rtxtool"])

    # perf's control fifo. `--delay=-1` starts the counters off and `bench` turns them on around the
    # frames it measures, so the recording is those frames: not the engine starting, not a cell
    # coming off the disk, not the renderer being taken down.
    control = out / "control.fifo"
    control.unlink(missing_ok=True)
    os.mkfifo(control)
    try:
        # **The ring buffer is perf's own default**, because `perf_event_mlock_kb` is 2048 on this
        # box and `-m` above that is refused rather than clamped. A dropped sample is counted.
        record = ["perf", "record", "--delay=-1", f"--control=fifo:{control}", "-o", str(data)]
        if mode == "offcpu":
            # `dummy` never fires: it gives perf an event to attach to, so the only samples in the
            # file are the BPF profiler's. A millisecond, against a default of five hundred: half a
            # second is thirty frames, so the default would see none of the waits a frame is made of.
            record += ["-e", "dummy", "--off-cpu", "--off-cpu-thresh", "1", "--call-graph", "fp"]
        elif unwind == "dwarf":
            # The default 8 KiB of stack per sample truncates OpenMW's deeper traversals, and a
            # truncated DWARF unwind looks complete and stops in the middle.
            record += ["-e", "task-clock", "-F", frequency, "--call-graph", "dwarf,32768"]
        else:
            # **`task-clock` rather than `cycles`, because this machine's CPU is hybrid**: `cycles`
            # is two events, one per core type, and a thread that migrated splits across both.
            # `task-clock` is one software event, and it counts nanoseconds, which a frame budget is
            # denominated in.
            record += ["-e", "task-clock", "-F", frequency, "--call-graph", "fp"]

        bench = [str(build.binary("openmw-rtxtool")), "bench", "--validation=off", "--window=false", *place,
                 f"--perf-control={control}", *extra]
        with open(out / "bench.txt", "w") as log:
            if mode == "offcpu":
                code = _record_offcpu(build, record, bench, data, log)
            else:
                recorded = subprocess.Popen([*record, "--", *bench], cwd=build.dir, env=build.env,
                                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                _tee(recorded, log)
                code = recorded.wait()
    finally:
        control.unlink(missing_ok=True)
    if code != 0:
        return code
    _report(data, out, "blocked" if mode == "offcpu" else "cpu", mode == "offcpu")
    return 0


def _perf_report(*args: str) -> str:
    return subprocess.run(["perf", "report", *args], capture_output=True, text=True).stdout


def _narrow(line: str, width: int = 86) -> str:
    """The symbol column, narrowed to something a terminal can hold. Templates make a C++ symbol as
    long as it likes, and the part that identifies it is at the front."""
    return line if len(line) <= width else line[:width - 1] + "…"


def _rows(text: str) -> list[list[str]]:
    """A report's rows, a list of fields each, without its headers."""
    return [line.split() for line in text.splitlines() if re.match(r"^ +[0-9]", line)]


def _symbol(line: str) -> str:
    return re.sub(r" {2,}.*$", "", line[line.index("] ") + 2:]) if "] " in line else ""


def _report(data: Path, out: Path, slug: str, blocked: bool) -> None:
    """**What the run itself said it measured**, which is exactly what the recording is bounded to:
    every figure is per second of that window, so it means the same whether the profile covers one
    place or six.

    **`--no-inline` throughout.** perf resolves the inline stack at a return address, and at this
    optimisation level that address usually lands in whatever was inlined *after* the call — so a
    chain through `placeScene` comes back as `~basic_string`. Real symbols and a chain that can be
    read beat inline names that name the wrong thing."""
    wall = 0.0
    frames = 0.0
    for line in (out / "bench.txt").read_text(errors="replace").splitlines():
        if re.search(r"frames in .* s ", line):
            fields = line.split()
            frames += float(fields[0])
            wall += float(fields[3])

    common = ["-i", str(data), "--stdio", "--no-inline"]
    summary = _perf_report(*common, "-g", "none", "--sort", "dso")
    lost = next((line.split()[-1] for line in summary.splitlines() if "Total Lost Samples" in line), None)
    nanoseconds = next((line.split()[-1] for line in summary.splitlines() if "Event count" in line), None)

    # Last run's reports go before this one's are written, so a file a mode no longer writes cannot
    # be read as part of this profile.
    for stale in [*out.glob(f"{slug}-*.txt"), out / f"{slug}.folded", out / f"{slug}.svg"]:
        stale.unlink(missing_ok=True)

    reports = {
        "libraries": summary,
        "self": _perf_report(*common, "--no-children", "-g", "none", "--percent-limit", "0.3"),
        "total": _perf_report(*common, "--children", "-g", "none", "--sort", "symbol", "--percent-limit", "0.5"),
        "callers": _perf_report(*common, "--no-children", "-g", "graph,2,caller", "--percent-limit", "1"),
        # What `-g1` is in the release build for. A line resolves what a symbol cannot: the hottest
        # entry after `Group::traverse` is an address in libc until the line tables say `memmove`.
        "lines": _perf_report(*common, "--no-children", "-g", "none", "--sort", "srcline", "--percent-limit", "0.5"),
    }
    for name, text in reports.items():
        (out / f"{slug}-{name}.txt").write_text(text)

    # A flame graph if something on this box can fold a stack. Nothing depends on one: the reports
    # are the same data, and the callers file is the same shape read the other way up.
    folders = [("inferno-collapse-perf", "inferno-flamegraph"), ("stackcollapse-perf.pl", "flamegraph.pl")]
    folder = next((pair for pair in folders if shutil.which(pair[0])), None)
    if folder is not None:
        script = subprocess.run(["perf", "script", "-i", str(data), "--no-inline"], capture_output=True).stdout
        folded = subprocess.run([folder[0]], input=script, capture_output=True, check=True).stdout
        (out / f"{slug}.folded").write_bytes(folded)
        graph = subprocess.run([folder[1], "--title", slug], input=folded, capture_output=True, check=True).stdout
        (out / f"{slug}.svg").write_bytes(graph)

    print(f"\nprofile: {frames:g} frames over {wall:.4f} s")
    if lost not in (None, "0"):
        print(f"  {lost} samples lost — the ring buffer overflowed, lower --freq")
    if nanoseconds is not None and wall > 0:
        label, unit = ("blocked ", "thread-seconds of waiting per second") if blocked else ("on-CPU  ", "cores busy, all the time")
        print(f"  {label} {float(nanoseconds) / 1e9 / wall:6.2f} {unit}")

    # **Pass-through frames come out.** A frame that spent nothing itself and cost what the frame
    # above it cost is a link in a chain, not a place the time went. Dropping them starts the list
    # where the cost first divides, and keeps every frame that did work or where a chain forked.
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

    print("\n  by source line:")
    for fields in _rows(reports["lines"])[:8]:
        print(f"    {fields[0]:>7}  {fields[1]}")

    print("\n  by library — of the whole stack, and of the leaf:")
    for fields in _rows(summary)[:10]:
        print(f"    {fields[0]:>7} {fields[1]:>7}  {fields[2] if len(fields) > 2 else ''}")

    print()
    for file in [*sorted(out.glob(f"{slug}*.txt")), out / f"{slug}.svg", out / "bench.txt"]:
        if file.exists():
            print(f"  {file}")
    if not (out / f"{slug}.svg").exists():
        print("  (no flame graph: pacman -S inferno)")
    print("  omw profile --tui   to walk the call graph")


def _tee(process: subprocess.Popen, log: IO[str]) -> None:
    """What the run prints, to the terminal as it comes and to `bench.txt`."""
    assert process.stdout is not None
    for line in process.stdout:
        sys.stdout.write(line)
        log.write(line)


def _record_offcpu(build: Build, record: list[str], bench: list[str], data: Path, log: IO[str]) -> int:
    """**Off-CPU sampling is BPF, and BPF here is privileged.** A `perf` that carries the
    capabilities needs nothing more — `sudo setcap cap_perfmon,cap_bpf,cap_sys_ptrace+ep
    "$(command -v perf)"` — and one without runs under sudo. Asked of the binary rather than assumed
    either way: a `perf` upgrade drops what was set on the file, and a `sudo` asked for every time is
    a password prompt in front of a profile that did not need one. The harness runs as the user
    throughout, where it has a home, a Wayland socket and a GPU, and perf attaches to it."""
    elevate: list[str] = []
    perf = shutil.which("perf") or "perf"
    capabilities = subprocess.run(["getcap", perf], capture_output=True, text=True).stdout
    if "cap_bpf" not in capabilities:
        print("profile: perf carries no cap_bpf — it runs under sudo, and the harness does not")
        elevate = ["sudo"]
        subprocess.run(["sudo", "-v"], check=True)

    harness = subprocess.Popen(bench, cwd=build.dir, env=build.env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
    # The harness reads a cell before the first frame it measures, which is the time perf has to attach.
    time.sleep(0.5)
    if harness.poll() is not None:
        raise Refusal("the harness ended before perf could attach")
    recorder = subprocess.Popen([*elevate, *record, "-p", str(harness.pid)])
    _tee(harness, log)
    code = harness.wait()
    recorder.wait()
    # Only what root wrote is owned by root.
    if elevate:
        subprocess.run(["sudo", "chown", f"{os.getuid()}:{os.getgid()}", str(data)], check=True)
    return code
