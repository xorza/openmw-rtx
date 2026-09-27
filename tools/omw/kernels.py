"""`omw [flavour] kernels [--against=<file>]`: what each kernel is once its constants are fixed, as
one digest per tuple.

The exact answer to "does this change move a vanilla frame", asked of the shaders and not of a run:
two runs are the driver's code, and its code of a module is not a function of the module alone
(`RtxTool::DriverCache`). A module is specialized to each tuple of its boolean constants — a `uint`
stays at its default — optimized so the branches its constants decide are gone, stripped of its
source and of constants nothing reads, and digested by `openmw-rtx-spirv-digest`, which names every
id by what it is and not by its number (`Rtx::digestProgram` says how, and why
`spirv-opt --canonicalize-ids` could not). Equal digests are one program for that tuple as far as its
instructions can say; with `--against`, a file this wrote before, the tuples that moved are named
and the exit status says whether any did.

**The spec-constant operations are folded before `-O`, which holds no pass that folds them.**
Freezing makes the constants themselves constant and leaves `!HAS_MAPS` or `WATER && HAS_SEA` an
operation, which no branch pass reads through: unfolded, every tuple with the maps off digests the
specular half, and every tuple with no water the water. **What still stays is float arithmetic on
constants that the build marked `NoContraction`**, which the optimizer will not fold even where the
result is exact; a block behind such a sum can only make a tuple move that did not, and taking the
decoration off to fold it would let the optimizer reassociate what it must not."""

import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from omw.build import Build
from omw.system import EXE, Refusal, jobs

_MAGIC = 0x07230203
_OP_NAME = 5
_OP_DECORATE = 71
_OP_SPEC_CONSTANT_TRUE = 48
_OP_SPEC_CONSTANT_FALSE = 49
_DECORATION_SPEC_ID = 1


@dataclass(frozen=True)
class SpecBool:
    spec_id: int
    name: str


def _string(words: tuple[int, ...]) -> str:
    raw = b"".join(struct.pack("<I", word) for word in words)
    return raw.split(b"\0", 1)[0].decode("utf-8", errors="replace")


def spec_bools(module: bytes) -> list[SpecBool]:
    """The boolean specialization constants of a SPIR-V module, by `SpecId`, read off its binary:
    each named by its `OpName` where the module keeps one, and `constant<id>` where it does not."""
    if len(module) < 20 or len(module) % 4:
        raise Refusal("not a SPIR-V module: its length is no whole number of words past the header")
    order = "<" if struct.unpack_from("<I", module)[0] == _MAGIC else ">"
    words = struct.unpack(f"{order}{len(module) // 4}I", module)
    if words[0] != _MAGIC:
        raise Refusal("not a SPIR-V module: the magic number is wrong")

    names: dict[int, str] = {}
    spec_ids: dict[int, int] = {}
    bools: set[int] = set()
    at = 5
    while at < len(words):
        count, opcode = words[at] >> 16, words[at] & 0xFFFF
        if count == 0 or at + count > len(words):
            raise Refusal(f"a SPIR-V instruction at word {at} runs past the module")
        operands = words[at + 1:at + count]
        if opcode == _OP_NAME:
            names[operands[0]] = _string(operands[1:])
        elif opcode == _OP_DECORATE and operands[1] == _DECORATION_SPEC_ID:
            spec_ids[operands[0]] = operands[2]
        elif opcode in (_OP_SPEC_CONSTANT_TRUE, _OP_SPEC_CONSTANT_FALSE):
            bools.add(operands[1])
        at += count

    found = [SpecBool(spec_ids[target], names.get(target) or f"constant{spec_ids[target]}")
             for target in bools if target in spec_ids]
    return sorted(found, key=lambda constant: constant.spec_id)


@dataclass(frozen=True)
class Setting:
    """One setting of a module's boolean constants: what `spirv-opt` is handed, and how the listing
    names it."""
    constants: str
    label: str


def settings(constants: list[SpecBool]) -> list[Setting]:
    """Every tuple, the first constant the lowest bit."""
    every: list[Setting] = []
    for tuple_bits in range(1 << len(constants)):
        on = [(tuple_bits >> at) & 1 for at in range(len(constants))]
        every.append(Setting(
            " ".join(f"{c.spec_id}:{'true' if bit else 'false'}" for c, bit in zip(constants, on)),
            ",".join(f"{c.name}={bit}" for c, bit in zip(constants, on)),
        ))
    return every


def moved(before: dict[str, str], now: dict[str, str]) -> list[str]:
    """The tuples whose digest differs, and the ones only one side has, `(none)` on the other: a
    line each, the tuple, the digest before and the digest now."""
    lines = []
    for key in sorted(before.keys() | now.keys()):
        old, new = before.get(key, "(none)"), now.get(key, "(none)")
        if old != new:
            lines.append(f"{key} {old} {new}")
    return lines


def _keyed(lines: list[str]) -> dict[str, str]:
    keyed = {}
    for line in lines:
        module, label, digest = line.split()
        keyed[f"{module}/{label}"] = digest
    return keyed


def kernels(build: Build, args: list[str]) -> int:
    against: Path | None = None
    for arg in args:
        if arg.startswith("--against="):
            against = Path(arg.split("=", 1)[1])
        else:
            raise Refusal("kernels reads --against=<file> and nothing else")

    # The build's own lines on stderr, because the listing is what a caller keeps.
    build.build(["openmw-rtx-vulkan-shaders", "openmw-rtx-spirv-digest"], stdout=sys.stderr)

    named_optimizer = build.cache_value("OPENMW_SPIRV_OPT")
    if not named_optimizer or not Path(named_optimizer).is_file():
        raise Refusal(f"the {build.flavour} build names no spirv-opt")
    optimizer: str = named_optimizer
    # The optimizer the build strips its modules with, so the two cannot be different releases.
    disassembler = Path(optimizer).with_name("spirv-dis" + Path(optimizer).suffix)
    program_digest = build.dir / "components" / "rtxvulkan" / f"openmw-rtx-spirv-digest{EXE}"

    shaders = build.dir / "resources" / "rtx" / "shaders"
    sources = build.dir / "resources" / "rtx" / "shaders-source"
    work: list[tuple[Path, Setting]] = []
    for module in sorted(shaders.glob("*.spv")):
        # The constants off the module the driver is handed, their names off the same module with its
        # source, which keeps the ids it has.
        named = sources / module.name
        for setting in settings(spec_bools((named if named.is_file() else module).read_bytes())):
            work.append((module, setting))

    def digest(item: tuple[Path, Setting]) -> str:
        module, setting = item
        label = setting.label or "-"
        specialize = ["--set-spec-const-default-value", setting.constants] if setting.constants else []
        with tempfile.TemporaryDirectory() as scratch:
            specialized = Path(scratch) / "specialized.spv"
            optimized = subprocess.run(
                [optimizer, "--target-env=vulkan1.4", "--scalar-block-layout", *specialize, "--freeze-spec-const",
                 "--fold-spec-const-op-composite", "-O", "--eliminate-dead-const", "--strip-debug",
                 "--strip-nonsemantic", module, "-o", specialized], capture_output=True)
            if optimized.returncode != 0:
                raise Refusal(f"spirv-opt refused {module.name} at {label}: {optimized.stderr.decode(errors='replace')}")
            disassembly = subprocess.run([disassembler, "--raw-id", "--no-header", specialized],
                                         capture_output=True, check=True).stdout
        hashed = subprocess.run([program_digest], input=disassembly, capture_output=True)
        if hashed.returncode != 0:
            raise Refusal(f"{module.name} at {label} could not be digested: {hashed.stderr.decode(errors='replace')}")
        return f"{module.name.removesuffix('.spv')} {label} {hashed.stdout.decode().strip()}"

    with ThreadPoolExecutor(max_workers=jobs()) as pool:
        listed = sorted(pool.map(digest, work))

    if against is None:
        print("\n".join(listed))
        return 0

    changed = moved(_keyed(against.read_text().splitlines()), _keyed(listed))
    if not changed:
        print(f"kernels: all {len(listed)} the same as {against}")
        return 0
    print(f"kernels: {len(changed)} of {len(listed)} moved against {against} (tuple, before, now):")
    print("\n".join(changed))
    return 1
