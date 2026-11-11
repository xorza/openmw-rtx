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
# libraries carry their debug information (`/Z7`), packs to near that: so a set is cut into parts of this
# size, which a build joins in order.
PART_SIZE = 1 << 30


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


def pack_set(exported: Path, triplet: str, part_size: int = PART_SIZE) -> list[Path]:
    """**Packs vcpkg's export of the set in `exported` as the parts `set_parts(triplet)` lists**, replacing
    the ones there, but the debug information of the host tools (`tools/**/*.pdb`): programs the build runs,
    whose symbols no crash report reads, and a gigabyte of Qt's on Windows."""
    staging = deps.VCPKG_SETS / f"{triplet}.partial"
    shutil.rmtree(staging, ignore_errors=True)
    staging.mkdir(parents=True)
    tools = f"{triplet}/installed/{triplet}/tools/"

    def kept(member: tarfile.TarInfo) -> tarfile.TarInfo | None:
        return None if member.name.startswith(tools) and member.name.endswith(".pdb") else member

    stream = _Parts(staging / f"vcpkg-{triplet}.tar.gz", part_size)
    # gzip and not xz: a set is gigabytes of static libraries, and Python's xz is one core. Its own stream
    # around the tar's, as a stream-mode tar takes no compression level before Python 3.12.
    with (stream, gzip.GzipFile(fileobj=stream, mode="wb", compresslevel=6, mtime=0) as zipped,
          tarfile.open(fileobj=zipped, mode="w|") as packed):
        packed.add(exported, arcname=triplet, filter=kept)
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
    check_container()
    triplet = deps.TRIPLET
    root = deps.vcpkg_root()
    work = deps.VCPKG_WORK
    installed = work / "installed"
    env = {**os.environ, "VCPKG_ROOT": str(root), "VCPKG_DISABLE_METRICS": "1",
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
    parts = pack_set(exported / triplet, triplet)
    # The export is the set this desk then builds against, as the published one would be once fetched.
    digest = deps.set_digest()
    folder = deps.vcpkg_set_dir(digest)
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    (exported / triplet).replace(folder / triplet)
    exported.rmdir()
    print(f"deps-{digest}: " + " ".join(str(part) for part in parts))
    return 0


def check_container() -> None:
    """Linux builds its set on the release base alone, with its GCC 15: a set built on a newer glibc would
    raise the release's floor to that glibc."""
    if SYSTEM != "linux":
        return
    if not package.on_release_base(OS_RELEASE.read_text() if OS_RELEASE.is_file() else ""):
        raise Refusal("the Linux set is built in AlmaLinux 8 (env/install_almalinux_deps.sh), "
                      "as .github/workflows/deps.yml does")
    if "gcc-toolset-15" not in os.environ.get("PATH", ""):
        raise Refusal("gcc-toolset-15 is not enabled: `source /opt/rh/gcc-toolset-15/enable`")
