"""Every download the driver pins, by version and SHA-256: the one file a bump edits, and what CI
keys its cache of deps/ on, so a bump saves the cache afresh. Why each is pinned the way it is stays with
the code that fetches it, in `deps.py`."""

from dataclasses import dataclass


@dataclass(frozen=True)
class Pin:
    url: str
    sha256: str


@dataclass(frozen=True)
class PublishedSet:
    """A dependency set the Dependency sets workflow published as the release `deps-<digest>`: each
    system's parts, by their SHA-256 in the order they join."""

    repository: str
    digest: str
    sha256: dict[str, tuple[str, ...]]

    def parts(self, system: str, triplet: str) -> list[Pin]:
        base = f"https://github.com/{self.repository}/releases/download/deps-{self.digest}/vcpkg-{triplet}.tar.gz"
        return [Pin(f"{base}.{index}", sha256) for index, sha256 in enumerate(self.sha256[system])]


# The clang-format every desk and CI formats with (`deps.clang_format`), by system: the PyPI wheels
# built from LLVM's release, about 2 MB each, where LLVM's own packages carry it inside 0.9 to 1.9 GB.
# The digests are PyPI's. Exact, since a point release moves the layout too.
CLANG_FORMAT_VERSION = "23.1.5"
CLANG_FORMAT = {
    "linux": Pin(
        "https://files.pythonhosted.org/packages/0e/cc/06adcfb7dfaea4d2c1929202021d4afcdc26915dc75fc0c72d63855da553/"
        f"clang_format-{CLANG_FORMAT_VERSION}-py2.py3-none-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl",
        "d97ce49911ba8c89ef3f3c0735a9c7818e4780ab4812c67d1c5fcd829d16ae52",
    ),
    "windows": Pin(
        "https://files.pythonhosted.org/packages/7b/67/b91fb3483ca5cdb468fa5e72d836736de5ec29154cc3f58718de6192bc12/"
        f"clang_format-{CLANG_FORMAT_VERSION}-py2.py3-none-win_amd64.whl",
        "18a4a1a698dd52ed4e288c97d99e93910f8f9d0128b266f65021ecf39c85d034",
    ),
}

# gersemi, the CMake files' formatter (`deps.gersemi`), by system and by the CPython it runs under: its
# layout is computed in Rust, built into each wheel for one interpreter's ABI, so the wheel follows the
# Python that runs the driver, 3.10 and later. The digests are PyPI's. Exact, as clang-format's is.
GERSEMI_VERSION = "0.29.2"
GERSEMI = {
    ("linux", "cp310"): Pin(
        "https://files.pythonhosted.org/packages/d7/d6/25dfffc7f8f6dbca98cf76059efaa0525d7ab1b3f97de5ec9e42afc298e1/"
        f"gersemi-{GERSEMI_VERSION}-cp310-cp310-manylinux_2_28_x86_64.whl",
        "f26b4cb7c1d9b9ab21109c9ebe226616f7c9e2ae9773056769d5421207e21e3d",
    ),
    ("linux", "cp311"): Pin(
        "https://files.pythonhosted.org/packages/76/73/771113af6c910d6d2b2fcb6b5acda563093cca54b72ff86666e77700f253/"
        f"gersemi-{GERSEMI_VERSION}-cp311-cp311-manylinux_2_28_x86_64.whl",
        "144ef0cabad485eedc03786814e1a2b651043427f99867d5aba7086b9d30d9a4",
    ),
    ("linux", "cp312"): Pin(
        "https://files.pythonhosted.org/packages/62/a0/3e8f12e8277eb679c660addb6fb45d0e150ba74d752f3582e32553347b50/"
        f"gersemi-{GERSEMI_VERSION}-cp312-cp312-manylinux_2_28_x86_64.whl",
        "45fff157fa8bd6920d043aed45f6d8d6ac5bf7b986d042bae6bfed896c888ecd",
    ),
    ("linux", "cp313"): Pin(
        "https://files.pythonhosted.org/packages/3b/c7/f6461fe5fd173fe40798b20cf47dae1e102bfea6a55dcf045898f41effe4/"
        f"gersemi-{GERSEMI_VERSION}-cp313-cp313-manylinux_2_28_x86_64.whl",
        "6a13296e33c027b4e438ec20602594b4ce36774712ba99a4ea23fcf47fd5e973",
    ),
    ("linux", "cp314"): Pin(
        "https://files.pythonhosted.org/packages/04/53/f2815b34359959172a6931932adde8e631de727a697a858265690030b55b/"
        f"gersemi-{GERSEMI_VERSION}-cp314-cp314-manylinux_2_28_x86_64.whl",
        "a8a851f3af50e185beff001e4666b24f4bb24b3f2810671eb4d04b30fff7066c",
    ),
    ("windows", "cp310"): Pin(
        "https://files.pythonhosted.org/packages/0b/ab/6ba9db916104b7de493d450a513735ab024892079185b71177b295245774/"
        f"gersemi-{GERSEMI_VERSION}-cp310-cp310-win_amd64.whl",
        "5f5b7c1e66d13caea2810fe39ce11493e769beb236fd435925e9fa5a80d4e3ab",
    ),
    ("windows", "cp311"): Pin(
        "https://files.pythonhosted.org/packages/b0/41/8552036195824b40ee18ec208227f8aeb7c2c2289116df5b36317512b129/"
        f"gersemi-{GERSEMI_VERSION}-cp311-cp311-win_amd64.whl",
        "a738e05edd342cc63a3df188eec185cd0fde4e0c8cf52026c409bf5b55c26401",
    ),
    ("windows", "cp312"): Pin(
        "https://files.pythonhosted.org/packages/74/55/8bc4a494b369d463115c9071d9e13c1f341adfa9ae23dee568c0dc36e3d0/"
        f"gersemi-{GERSEMI_VERSION}-cp312-cp312-win_amd64.whl",
        "76b0d29af1412045b0be37d3e7c240146638bcaecab6e7e7866644a54a283b6a",
    ),
    ("windows", "cp313"): Pin(
        "https://files.pythonhosted.org/packages/7e/e5/5161c7486b26353b5d0e49bcbaac370223824866a8c4045f680b7184f0ec/"
        f"gersemi-{GERSEMI_VERSION}-cp313-cp313-win_amd64.whl",
        "5f394ea9b2301e4834cfb072ee312d691aa71e16fe7c722f08368310aac220b9",
    ),
    ("windows", "cp314"): Pin(
        "https://files.pythonhosted.org/packages/2f/a3/def49b8e304277a7c28b2c5f9c538bedb71f972c73449d505e9fa769af43/"
        f"gersemi-{GERSEMI_VERSION}-cp314-cp314-win_amd64.whl",
        "50d60f5470cf80baa07bdf993e5d9add610032d14d83e4baa6f9d248cd2811c0",
    ),
}

# PyYAML, which gersemi reads its configuration with: the source release's pure-Python package, one for
# every interpreter, where each wheel carries a C extension built for one.
PYYAML = Pin(
    "https://files.pythonhosted.org/packages/05/8e/961c0007c59b8dd7729d542c61a4d537767a59645b82a0b521206e1e25c2/"
    "pyyaml-6.0.3.tar.gz",
    "d76623373421df22fb4cf8817020cbb7ef15c725b9d5e45f17e189bfc384190f",
)

# The tools an AppImage is made with, by the name each is kept under in its folder of deps/
# (`deps.appimage_tools`).
APPIMAGE_TOOLS = {
    "linuxdeploy": Pin(
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage",
        "c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d",
    ),
    "linuxdeploy-plugin-appimage": Pin(
        "https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-appimage-x86_64.AppImage",
        "992d502a248e14ab185448ddf6f6e7d25558cb84d4623c354c3af350c25fccb3",
    ),
    "runtime-x86_64": Pin(
        "https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64",
        "2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d",
    ),
}

# The Ninja every build runs (`deps.ninja`), by system: Ninja's own release, whose Linux binary asks glibc 2.15,
# where AlmaLinux 8's Ninja is 1.8.2 and refuses the files CMake 4.4 writes. The digests are of the releases'
# archives.
NINJA_VERSION = "1.13.2"
NINJA = {
    "linux": Pin(
        f"https://github.com/ninja-build/ninja/releases/download/v{NINJA_VERSION}/ninja-linux.zip",
        "5749cbc4e668273514150a80e387a957f933c6ed3f5f11e03fb30955e2bbead6",
    ),
    "windows": Pin(
        f"https://github.com/ninja-build/ninja/releases/download/v{NINJA_VERSION}/ninja-win.zip",
        "07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65",
    ),
}

# Clang 23, every Linux build's compiler and linker (`deps.llvm`): kernel.org's slim build of LLVM's release, made on Debian 10 so
# it runs on AlmaLinux 8's glibc 2.28, where LLVM's own Linux release asks a newer one. The digest is the one
# kernel.org signs in `sha256sums.asc` beside it.
LLVM = Pin(
    "https://www.kernel.org/pub/tools/llvm/files/llvm-23.1.5-x86_64.tar.xz",
    "b69433120ad62420d789f601c58a5472873c724be088c99a70b72fd30ca4af3c",
)

# Breakpad's two tools, by name and system: `dump_syms` turns a release's debug information into the
# symbol files each release publishes, and `minidump-stackwalk` reads a player's dump against them.
CRASH_TOOLS = {
    ("dump_syms", "linux"): Pin(
        "https://github.com/mozilla/dump_syms/releases/download/v2.3.9/dump_syms-x86_64-unknown-linux-gnu.tar.xz",
        "0fc852a86b00337407d9d423cc388a24c3b489ccaaedcf92623cad57af5ca8ad",
    ),
    ("dump_syms", "windows"): Pin(
        "https://github.com/mozilla/dump_syms/releases/download/v2.3.9/dump_syms-x86_64-pc-windows-msvc.zip",
        "bdf48486220708808a3e00aec78856ee1cce096189d47a1e2cb1c635f93bacc7",
    ),
    ("minidump-stackwalk", "linux"): Pin(
        "https://github.com/rust-minidump/rust-minidump/releases/download/v0.27.0/minidump-stackwalk-x86_64-unknown-linux-gnu.tar.xz",
        "0020324c54cc359596e927ee907204f4c8d4da6718765536edcabaa1622122ad",
    ),
    ("minidump-stackwalk", "windows"): Pin(
        "https://github.com/rust-minidump/rust-minidump/releases/download/v0.27.0/minidump-stackwalk-x86_64-pc-windows-msvc.zip",
        "f6f2d7f1665843c4a270cd13fcd1458fed3b19013ea5dd909e12bc3599b959f4",
    ),
}

# The Vulkan loader Windows' tests start against (`deps.vulkan_loader`): the runtime components LunarG
# publishes for redistribution, with the checksum LunarG lists for the file in sdk.lunarg.com/sdk/files.json.
VULKAN_LOADER_WINDOWS_VERSION = "1.4.363.0"
VULKAN_LOADER_WINDOWS = Pin(
    f"https://sdk.lunarg.com/sdk/download/{VULKAN_LOADER_WINDOWS_VERSION}/windows/VulkanRT-X64-{VULKAN_LOADER_WINDOWS_VERSION}-Components.zip",
    "a25a927aa8b9f0371048f1861cf88ac3b9bc9b1fb332c42d897c8ab32695769a",
)
# vcpkg, which builds the libraries `vcpkg.json` names (`deps.vcpkg_root`): its registry at the commit of
# release 2026.10.16, whose ports fix every library's version, and the tool's newest release, 2026-10-29,
# by system, newer than the 2026-09-26 the registry's `scripts/vcpkg-tool-metadata.txt` names: the set
# built with the two is what holds them together. The registry's digest is of GitHub's archive of that
# commit; the tools' are the SHA-256 Microsoft's release lists for each.
VCPKG_REGISTRY_COMMIT = "9eec002df1cd109f8f104cd966a763daf9751489"
VCPKG_REGISTRY = Pin(
    f"https://github.com/microsoft/vcpkg/archive/{VCPKG_REGISTRY_COMMIT}.tar.gz",
    "0e41e3fc97abf18bd84f52e711eff88ba88726aa316c4f0f41d5bbd7d7b4690e",
)
VCPKG_TOOL = {
    "linux": Pin(
        "https://github.com/microsoft/vcpkg-tool/releases/download/2026-10-29/vcpkg-glibc",
        "8b10d43c1392a2d51622b1e439cb952289dcf56cbb595eb935847e79fe00d303",
    ),
    "windows": Pin(
        "https://github.com/microsoft/vcpkg-tool/releases/download/2026-10-29/vcpkg.exe",
        "191ee64b0887af80aad3eff6c5e888ab733d3ef2182885ed4747b4574c201834",
    ),
}

# The dependency set every build links (`deps.vcpkg_set`): the release the Dependency sets workflow published
# for the inputs `deps.set_digest` reads, each system's parts by the SHA-256 its `SHA256SUMS` lists.
VCPKG_SET = PublishedSet(
    repository="xorza/openmw-rtx",
    digest="19e1354572579115",
    sha256={
        "linux": ("556a28dba59a1288de10be4f75ffbf1d526ea0e6d228b9cf47dcf35b670efd8e",),
        "windows": ("7aca6919375d0233f3d63dada1f399e2f2eb7ff3e837d068e36e450d58a192d6",
                    "7f223a237533c67bbc31bbee87976a7695d822e6d3450a72d309aeef974a3c2f"),
    },
)

# The CMake every build of the driver's configures with, on the desk, on CI and in the release container
# (`deps.cmake`), by system: the release `cmake_minimum_required` names. The digests are Kitware's own
# (`cmake-<version>-SHA-256.txt`).
CMAKE_VERSION = "4.4.5"
CMAKE = {
    "linux": Pin(
        f"https://github.com/Kitware/CMake/releases/download/v{CMAKE_VERSION}/cmake-{CMAKE_VERSION}-linux-x86_64.tar.gz",
        "ed85135c5a5135b45e50423cb406dea04ca3338da0b8ef3c9ede963559680be0",
    ),
    "windows": Pin(
        f"https://github.com/Kitware/CMake/releases/download/v{CMAKE_VERSION}/cmake-{CMAKE_VERSION}-windows-x86_64.zip",
        "326c826486d46a23cf9d5a1c9ee3a567d427bdd02c4a34071937a2a675cff004",
    ),
}
