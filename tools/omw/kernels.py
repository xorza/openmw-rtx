"""`omw [flavour] kernels [--against=<file>]`: what each kernel is once its constants are fixed, as
one digest per tuple.

The exact answer to "does this change move a vanilla frame", asked of the shaders and not of a run:
two runs are the driver's code, and its code of a module is not a function of the module alone
(`RtxTool::DriverCache`). A module is specialized to each tuple of its constants — a boolean both
ways, and a `uint` over the domain `DOMAINS` reads for it from the header that sizes it, so no
constant stays at its default unnoticed — optimized so the branches its constants decide are gone, stripped of its
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

import itertools
import re
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from omw.build import Build
from omw.system import EXE, ROOT, Refusal, Switches, jobs, read_text

_MAGIC = 0x07230203
_OP_NAME = 5
_OP_DECORATE = 71
_OP_TYPE_INT = 21
_OP_SPEC_CONSTANT_TRUE = 48
_OP_SPEC_CONSTANT_FALSE = 49
_OP_SPEC_CONSTANT = 50
_DECORATION_SPEC_ID = 1


@dataclass(frozen=True)
class SpecConstant:
    """A specialization constant, and every value a tuple takes it at: both, for a boolean."""
    spec_id: int
    name: str
    values: tuple[int, ...]
    boolean: bool = False


def header_count(header: str, name: str) -> int:
    """The `const uint <name> = <n>;` a shared header states, as the build reads it."""
    found = re.search(rf"\bconst\s+uint\s+{name}\s*=\s*(\d+)u?\s*;", header)
    if found is None:
        raise Refusal(f"no `const uint {name}` in the header that sizes it")
    return int(found.group(1))


def domains() -> dict[str, tuple[int, ...]]:
    """**Each unsigned constant's values, by its `OpName`, read from the header that sizes it**, as
    `listing.py` reads the CMake text: the shadow filter's level, one module per level."""
    shadow = read_text(ROOT / "components" / "rtxvulkan" / "shaders" / "shared" / "shadow.h")
    return {"SHADOW_LEVEL": tuple(range(header_count(shadow, "SHADOW_FILTER_LEVELS")))}


def _string(words: tuple[int, ...]) -> str:
    raw = b"".join(struct.pack("<I", word) for word in words)
    return raw.split(b"\0", 1)[0].decode("utf-8", errors="replace")


def spec_constants(module: bytes, known: dict[str, tuple[int, ...]]) -> list[SpecConstant]:
    """The specialization constants of a SPIR-V module, by `SpecId`, read off its binary: each named
    by its `OpName` where the module keeps one, and `constant<id>` where it does not. A boolean takes
    both values; an unsigned integer takes the values `known` holds under its name, and **one with no
    domain is refused**, as is a constant of any other type: left at its default, a tuple the module
    can be specialized to would go undigested in silence."""
    if len(module) < 20 or len(module) % 4:
        raise Refusal("not a SPIR-V module: its length is no whole number of words past the header")
    order = "<" if struct.unpack_from("<I", module)[0] == _MAGIC else ">"
    words = struct.unpack(f"{order}{len(module) // 4}I", module)
    if words[0] != _MAGIC:
        raise Refusal("not a SPIR-V module: the magic number is wrong")

    names: dict[int, str] = {}
    spec_ids: dict[int, int] = {}
    bools: set[int] = set()
    unsigned_types: set[int] = set()
    numbers: dict[int, int] = {}
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
        elif opcode == _OP_TYPE_INT and operands[1] == 32 and operands[2] == 0:
            unsigned_types.add(operands[0])
        elif opcode == _OP_SPEC_CONSTANT:
            numbers[operands[1]] = operands[0]
        at += count

    found = []
    for target, spec_id in spec_ids.items():
        name = names.get(target) or f"constant{spec_id}"
        if target in bools:
            found.append(SpecConstant(spec_id, name, (0, 1), boolean=True))
        elif target in numbers and numbers[target] in unsigned_types and name in known:
            found.append(SpecConstant(spec_id, name, known[name]))
        elif target in numbers:
            kind = "an unsigned" if numbers[target] in unsigned_types else "a"
            raise Refusal(f"{kind} specialization constant {name} has no domain to digest it over; give it one "
                          "in `kernels.domains`")
    return sorted(found, key=lambda constant: constant.spec_id)


@dataclass(frozen=True)
class Setting:
    """One setting of a module's constants: what `spirv-opt` is handed, and how the listing names
    it."""
    constants: str
    label: str


def _value(constant: SpecConstant, value: int) -> str:
    if constant.boolean:
        return "true" if value else "false"
    return str(value)


def settings(constants: list[SpecConstant]) -> list[Setting]:
    """Every tuple, the first constant the fastest to change: the lowest bit, among booleans."""
    every: list[Setting] = []
    for reversed_values in itertools.product(*(constant.values for constant in reversed(constants))):
        values = reversed_values[::-1]
        every.append(Setting(
            " ".join(f"{c.spec_id}:{_value(c, value)}" for c, value in zip(constants, values)),
            ",".join(f"{c.name}={value}" for c, value in zip(constants, values)),
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


def keyed(lines: list[str], source: str) -> dict[str, str]:
    """A listing's lines by module and tuple. **A line that is not `<module> <tuple> <digest>` is refused**:
    the file `--against` names is whatever was redirected into it, a build's own lines or an error among
    them."""
    found = {}
    for number, line in enumerate(lines, 1):
        fields = line.split()
        if len(fields) != 3:
            raise Refusal(f"{source}:{number} is no `<module> <tuple> <digest>` line of a listing: {line!r}")
        module, label, digest = fields
        found[f"{module}/{label}"] = digest
    return found


def kernels(build: Build, args: list[str]) -> int:
    switches = Switches("kernels", "one digest per shader and tuple of its constants")
    switches.add_argument("--against", type=Path, help="a listing this wrote before: name the tuples that moved")
    against: Path | None = switches.parse_args(args).against

    # The build's own lines on stderr, because the listing is what a caller keeps.
    build.build(["openmw-rtx-vulkan-shaders", "openmw-rtx-spirv-digest"], stdout=sys.stderr)

    named_optimizer = build.cache_value("OPENMW_SPIRV_OPT")
    if not named_optimizer or not Path(named_optimizer).is_file():
        raise Refusal(f"the {build.flavour} build names no spirv-opt")
    optimizer: str = named_optimizer
    # The optimizer the build strips its modules with, so the two cannot be different releases.
    disassembler = Path(optimizer).with_name("spirv-dis" + Path(optimizer).suffix)
    if not disassembler.is_file():
        raise Refusal(f"there is no spirv-dis beside {optimizer}")
    program_digest = build.dir / "components" / "rtxvulkan" / f"openmw-rtx-spirv-digest{EXE}"

    shaders = build.cached_folder("RTX_SPIRV_DIR")
    sources = build.cached_folder("RTX_SPIRV_SOURCE_DIR")
    known = domains()
    work: list[tuple[Path, Setting]] = []
    for module in sorted(shaders.glob("*.spv")):
        # The constants off the module the driver is handed, their names off the same module with its
        # source, which keeps the ids it has.
        named = sources / module.name
        for setting in settings(spec_constants((named if named.is_file() else module).read_bytes(), known)):
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
                 "--strip-nonsemantic", module, "-o", specialized], capture_output=True, check=False)
            if optimized.returncode != 0:
                said = optimized.stderr.decode(errors="replace")
                raise Refusal(f"spirv-opt refused {module.name} at {label}: {said}")
            disassembly = subprocess.run([disassembler, "--raw-id", "--no-header", specialized],
                                         capture_output=True, check=True).stdout
        hashed = subprocess.run([program_digest], input=disassembly, capture_output=True, check=False)
        if hashed.returncode != 0:
            said = hashed.stderr.decode(errors="replace")
            raise Refusal(f"{module.name} at {label} could not be digested: {said}")
        return f"{module.name.removesuffix('.spv')} {label} {hashed.stdout.decode().strip()}"

    with ThreadPoolExecutor(max_workers=jobs()) as pool:
        listed = sorted(pool.map(digest, work))

    if against is None:
        print("\n".join(listed))
        return 0

    if not against.is_file():
        raise Refusal(f"there is no listing at {against}")
    changed = moved(keyed(read_text(against).splitlines(), str(against)), keyed(listed, "the listing"))
    if not changed:
        print(f"kernels: all {len(listed)} the same as {against}")
        return 0
    print(f"kernels: {len(changed)} of {len(listed)} moved against {against} (tuple, before, now):")
    print("\n".join(changed))
    return 1
