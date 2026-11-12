"""`omw deps-set [--digest]`: the libraries `vcpkg.json` names, built static by vcpkg for this system's
triplet and packed into the parts of one archive under `deps/sets/`, which CI publishes as the release named after the
set's digest; or, with `--digest`, that name alone. Linux builds its set on the release base, AlmaLinux
8, whose glibc is older than every system the set serves: in its container (`env/release-base.Dockerfile`)
on a desk that is another system, as `omw archive` builds there."""

import gzip
import io
import os
import shutil
import tarfile
from pathlib import Path

from omw import deps, package
from omw.system import EXE, ROOT, SYSTEM, Refusal, run

# The file a Linux system names itself in.
OS_RELEASE = Path("/etc/os-release")

# GitHub refuses a release asset of 2 GiB or more ("About releases", docs.github.com), and Windows' set, whose
# libraries carry MSVC's full debug information (`/Z7`), packs to more than half of that: so a set is cut into
# parts of this size, which a build joins in order.
PART_SIZE = 1 << 30

# The ports the set carries for their programs alone, which the build runs (`glslc`, `spirv-opt`, `spirv-val`,
# found by `find_program`) and links nothing of: what they install outside `tools/` is pruned.
TOOL_PORTS = ("shaderc", "glslang", "spirv-tools")


def set_parts(triplet: str) -> list[Path]:
    """The parts of the set packed for `triplet`, in the order they join."""
    return sorted(deps.VCPKG_SETS.glob(f"vcpkg-{triplet}.tar.gz.*"), key=lambda part: int(part.suffix[1:]))


class _Parts(io.RawIOBase):
    """A stream written into files of `PART_SIZE` bytes, `<name>.0` on, the last one shorter."""

    def __init__(self, base: Path, part_size: int = PART_SIZE):
        super().__init__()
        self.base = base
        self.part_size = part_size
        self.parts: list[Path] = []
        self.file: io.BufferedWriter | None = None
        self.left = 0

    def writable(self) -> bool:
        return True

    def write(self, data) -> int:  # type: ignore[override]
        view = memoryview(data).cast("B")
        while view:
            if self.file is None or self.left == 0:
                self._next()
            assert self.file is not None
            taken = min(self.left, len(view))
            self.file.write(view[:taken])
            self.left -= taken
            view = view[taken:]
        return len(data)

    def _next(self) -> None:
        if self.file is not None:
            self.file.close()
        self.parts.append(self.base.with_name(f"{self.base.name}.{len(self.parts)}"))
        self.file = self.parts[-1].open("wb")
        self.left = self.part_size

    def close(self) -> None:
        if self.file is not None:
            self.file.close()
            self.file = None
        super().close()


def prune_set(exported: Path, triplet: str) -> None:
    """**Takes out of vcpkg's export in `exported` what no build reads**, in place, so the set this desk keeps
    is the set it packs: the debug information of the host tools, programs the build runs, whose symbols no
    crash report reads (`tools/**/*.pdb`, a gigabyte of Qt's on Windows; the ELF programs stripped), and what
    `TOOL_PORTS` install beside their programs, by vcpkg's own record of each port's files."""
    installed = exported / "installed" / triplet
    for pdb in sorted((installed / "tools").rglob("*.pdb")):
        pdb.unlink()
    info = exported / "installed" / "vcpkg" / "info"
    for port in TOOL_PORTS:
        listings = list(info.glob(f"{port}_*_{triplet}.list"))
        assert len(listings) == 1, f"{port} is not in the set once: {listings}"
        entries = [line.removeprefix(f"{triplet}/") for line in listings[0].read_text().splitlines()]
        pruned = [entry for entry in entries if entry and not entry.startswith("tools/")]
        for entry in pruned:
            if not entry.endswith("/"):
                (installed / entry).unlink()
        # A folder the port shares with another, `include/` or `lib/`, keeps the other's files.
        for entry in sorted((entry for entry in pruned if entry.endswith("/")), key=len, reverse=True):
            folder = installed / entry
            if not any(folder.iterdir()):
                folder.rmdir()
    programs = [path for folder in (installed / "tools", installed / "bin") for path in sorted(folder.rglob("*"))
                if _elf_program(path)]
    if programs:
        run(["strip", "--strip-all", *programs])


def _elf_program(path: Path) -> bool:
    """An ELF executable or shared object, which `strip --strip-all` leaves runnable: not an object file,
    whose relocations need its symbols."""
    if path.is_symlink() or not path.is_file():
        return False
    with path.open("rb") as file:
        header = file.read(18)
    # e_type, at byte 16 in the ELF header's byte order, which byte 5 names: 2 executable, 3 shared.
    return (len(header) == 18 and header[:4] == b"\x7fELF"
            and int.from_bytes(header[16:18], "little" if header[5] == 1 else "big") in (2, 3))


def pack_set(exported: Path, triplet: str, part_size: int = PART_SIZE) -> list[Path]:
    """**Packs vcpkg's export of the set in `exported` as the parts `set_parts(triplet)` lists**, replacing
    the ones there."""
    staging = deps.VCPKG_SETS / f"{triplet}.partial"
    shutil.rmtree(staging, ignore_errors=True)
    staging.mkdir(parents=True)
    stream = _Parts(staging / f"vcpkg-{triplet}.tar.gz", part_size)
    # gzip and not xz: a set is gigabytes of static libraries, and Python's xz is one core. Its own stream
    # around the tar's, as a stream-mode tar takes no compression level before Python 3.12.
    with (stream, gzip.GzipFile(fileobj=stream, mode="wb", compresslevel=6, mtime=0) as zipped,
          tarfile.open(fileobj=zipped, mode="w|") as packed):
        packed.add(exported, arcname=triplet)
    for old in set_parts(triplet):
        old.unlink()
    for part in stream.parts:
        part.replace(deps.VCPKG_SETS / part.name)
    staging.rmdir()
    return set_parts(triplet)


def deps_set(args: list[str]) -> int:
    if args == ["--digest"]:
        print(f"deps-{deps.set_digest()}")
        return 0
    if args:
        raise Refusal("deps-set takes `--digest` or nothing")
    if SYSTEM == "linux" and not package.on_release_base(OS_RELEASE.read_text() if OS_RELEASE.is_file() else ""):
        run(package.release_container("release").command(["python3", "omw", "deps-set"]))
        return 0
    compilers = container_compilers()
    triplet = deps.TRIPLET
    root = deps.vcpkg_root()
    work = deps.VCPKG_WORK
    installed = work / "installed"
    env = {**os.environ, **compilers, "VCPKG_ROOT": str(root), "VCPKG_DISABLE_METRICS": "1",
           "VCPKG_BINARY_SOURCES": f"clear;files,{deps.VCPKG_BINARY_CACHE},readwrite"}
    deps.VCPKG_BINARY_CACHE.mkdir(parents=True, exist_ok=True)
    vcpkg = root / f"vcpkg{EXE}"
    roots = [f"--x-manifest-root={ROOT}", f"--x-install-root={installed}"]
    run([vcpkg, "install", f"--triplet={triplet}", f"--host-triplet={triplet}", *roots,
         f"--x-buildtrees-root={work / 'buildtrees'}", f"--x-packages-root={work / 'packages'}",
         f"--downloads-root={deps.VCPKG_DOWNLOADS}", "--clean-after-build"], env=env, cwd=ROOT)
    # vcpkg's own export, whose toolchain file (`scripts/buildsystems/vcpkg.cmake`) a build of the tree
    # configures with, and whose `installed/<triplet>` holds the libraries and the build's tools.
    exported = work / "export"
    shutil.rmtree(exported, ignore_errors=True)
    run([vcpkg, "export", "--x-all-installed", "--raw", f"--output-dir={exported}", f"--output={triplet}", *roots],
        env=env, cwd=ROOT)
    prune_set(exported / triplet, triplet)
    parts = pack_set(exported / triplet, triplet)
    # The export is the set this desk then builds against, as the published one would be once fetched.
    digest = deps.set_digest()
    deps.place_set(exported / triplet, digest)
    exported.rmdir()
    print(f"deps-{digest}: " + " ".join(str(part) for part in parts))
    return 0


def container_compilers() -> dict[str, str]:
    """**The compilers every port builds with, by path**: Linux builds its set on the release base alone, with
    the pinned Clang the tree builds with (`deps.compiler_environment`), against gcc-toolset-15's libstdc++
    (`vcpkg/linux.cmake`), as a set built on a newer glibc would raise the release's floor to that glibc. Named
    in `CC` and `CXX`, which CMake, Meson and Autotools all read, rather than left to `PATH`: a port that puts a
    folder of its own ahead on it (vcpkg's Meson puts Python's, `/usr/bin`) found AlmaLinux 8's own GCC 8.5 as
    `cc`. Windows' is the Visual Studio vcpkg finds."""
    if SYSTEM != "linux":
        return {}
    if not package.on_release_base(OS_RELEASE.read_text() if OS_RELEASE.is_file() else ""):
        raise Refusal("the Linux set is built in AlmaLinux 8 (env/install_almalinux_deps.sh), "
                      "as .github/workflows/deps.yml does")
    if "gcc-toolset-15" not in os.environ.get("PATH", ""):
        raise Refusal("gcc-toolset-15 is not enabled: `source /opt/rh/gcc-toolset-15/enable`")
    env: dict[str, str] = {}
    deps.compiler_environment(env)
    return env
