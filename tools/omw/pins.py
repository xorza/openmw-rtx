"""Every download the driver pins, by version and SHA-256: the one file a bump edits, and what CI
keys its cache of out/deps/ on, so a bump saves the cache afresh. Why each is pinned the way it is stays with
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
# Python that runs the driver, 3.11 and later. The digests are PyPI's. Exact, as clang-format's is.
GERSEMI_VERSION = "0.29.2"
GERSEMI = {
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

# Ruff, the driver's linter (`deps.ruff`), by system: the one program out of its PyPI wheel, as clang-format's is. The
# digests are PyPI's. Exact, since a release adds rules.
RUFF_VERSION = "0.17.4"
RUFF = {
    "linux": Pin(
        "https://files.pythonhosted.org/packages/af/1b/62874f14c7873f27c7887d2b1ea41de990e14ba9518801ffe8eacb5d51a6/"
        f"ruff-{RUFF_VERSION}-py3-none-manylinux_2_17_x86_64.manylinux2014_x86_64.whl",
        "2cd025509b2e15721b426fc70fd946d71f43b3db3b08e946dfec2d696d911690",
    ),
    "windows": Pin(
        "https://files.pythonhosted.org/packages/bd/15/42af8a72d8946e3c9efd2bbf6641e462b6832783b0d043d63ef5b280ba7f/"
        f"ruff-{RUFF_VERSION}-py3-none-win_amd64.whl",
        "67c5a894fc054cf41f75f7afa24815de5a74329d97634126c2a1015740ff4e8c",
    ),
}

# zizmor, the workflows' audit (`deps.zizmor`), by system: the one program out of its PyPI wheel. The digests are
# PyPI's. Exact, since a release adds audits.
ZIZMOR_VERSION = "1.32.0"
ZIZMOR = {
    "linux": Pin(
        "https://files.pythonhosted.org/packages/6f/b2/45f4fcb6f428e774750709ecf5320ba6d96393c03b941d9f37a4586f6924/"
        f"zizmor-{ZIZMOR_VERSION}-py3-none-manylinux_2_28_x86_64.whl",
        "84a6acb975accc13408786a2bd274b572537ca19c933ebbc13e78f3988562071",
    ),
    "windows": Pin(
        "https://files.pythonhosted.org/packages/0f/1f/1ed88cac316f248c7851860bd209ce2a2abf1a2481d887b8a78619dd4a39/"
        f"zizmor-{ZIZMOR_VERSION}-py3-none-win_amd64.whl",
        "9cd1dd7734042f40ca0faa2a31fb4e9033dede620d996af2f903744b8f04fca9",
    ),
}

# mypy, the driver's type check (`deps.mypy`), by system and by the CPython it runs under: the wheels mypyc compiled,
# each for one interpreter's ABI and far faster than the one of Python alone, so the wheel follows the Python that
# runs the driver, 3.11 and later. The digests are PyPI's.
MYPY_VERSION = "2.4.1"
MYPY = {
    ("linux", "cp311"): Pin(
        "https://files.pythonhosted.org/packages/c4/5a/379f9bfa18bf3f8d43668fbd9ae4f2922dd027a559c461ca9d41cde1d32e/"
        f"mypy-{MYPY_VERSION}-cp311-cp311-manylinux2014_x86_64.manylinux_2_17_x86_64.manylinux_2_28_x86_64.whl",
        "b69a15fc2e75858d6b15e91ed3a0dcff1a533265183b64734138791fe0f49aa5",
    ),
    ("linux", "cp312"): Pin(
        "https://files.pythonhosted.org/packages/5d/b8/7313212b29de570c25c9859d7c8e4ef2251a6d34338a1e20031d94297721/"
        f"mypy-{MYPY_VERSION}-cp312-cp312-manylinux2014_x86_64.manylinux_2_17_x86_64.manylinux_2_28_x86_64.whl",
        "a20ff6cd2af7a670c64f089bfa5fbcebe8ff3bf896c5e6ac7c9862c7960e4494",
    ),
    ("linux", "cp313"): Pin(
        "https://files.pythonhosted.org/packages/a2/45/089b4724b85f0f408941be5298f817ee89181c1ce031a73d60ebb150c2c7/"
        f"mypy-{MYPY_VERSION}-cp313-cp313-manylinux2014_x86_64.manylinux_2_17_x86_64.manylinux_2_28_x86_64.whl",
        "1fb70621535c22fd4b1a0450d37ed1d6c5eb39656a1cebfb189084c12de54ba7",
    ),
    ("linux", "cp314"): Pin(
        "https://files.pythonhosted.org/packages/16/10/4192a5cea09ebbc41c534f57bd0d8f0fb42a8e810632cda6e37f072c59fd/"
        f"mypy-{MYPY_VERSION}-cp314-cp314-manylinux2014_x86_64.manylinux_2_17_x86_64.manylinux_2_28_x86_64.whl",
        "beb254fec368c4e7f7f82a5f310c6470b893ec88a90b66426da971c6826b0453",
    ),
    ("windows", "cp311"): Pin(
        "https://files.pythonhosted.org/packages/6a/36/8b722b88c325d317354de7d10d8fb9d4dc0f958433e873a91fc4ed21a06e/"
        f"mypy-{MYPY_VERSION}-cp311-cp311-win_amd64.whl",
        "6f7225951a277dd0d6add900731d487174b7d24ac133c09d670ed63b4d8cb90d",
    ),
    ("windows", "cp312"): Pin(
        "https://files.pythonhosted.org/packages/b6/b4/874f6fd54b005020ad63e2cf7a59b178d16a771d478849b29b842cb0cf14/"
        f"mypy-{MYPY_VERSION}-cp312-cp312-win_amd64.whl",
        "4c194c5d878130fca1ae662dfb1feb6274afbab0b40032c80147377e0ea803d5",
    ),
    ("windows", "cp313"): Pin(
        "https://files.pythonhosted.org/packages/76/5b/7d160fb685994b65b0ca86d352f3afb5424f96a9e6914719f7b861fb6701/"
        f"mypy-{MYPY_VERSION}-cp313-cp313-win_amd64.whl",
        "acdd82cb3a2eb10720e87706a128d75785d005a8c47e36a4973221524f41fc56",
    ),
    ("windows", "cp314"): Pin(
        "https://files.pythonhosted.org/packages/cc/56/de9d7bad12e3868d5f1f4279aed20b1b568b2c5e166f2d3a05b211dca0b1/"
        f"mypy-{MYPY_VERSION}-cp314-cp314-win_amd64.whl",
        "51f742d4154824837ca6e92aa81f00eec222700a3c337bd258e5562a3880d75a",
    ),
}

# What mypy imports besides itself, each at the version its own `Requires-Dist` takes, pinned as mypy is so no part
# of the type check is whatever an index serves that day: librt, mypyc's runtime, built for one interpreter's ABI as
# mypy is; ast-serialize, built once for every CPython's stable ABI; and three packages of Python alone.
LIBRT_VERSION = "0.16.0"
LIBRT = {
    ("linux", "cp311"): Pin(
        "https://files.pythonhosted.org/packages/a0/e4/15a8d62b2bc12935080ec7277e2e0b9b233047781eb784a51d3e25af669a/"
        f"librt-{LIBRT_VERSION}-cp311-cp311-manylinux2014_x86_64.manylinux_2_17_x86_64.manylinux_2_28_x86_64.whl",
        "94aed6a8308818b91677957d1bd03188869cd7aeb23c5dba7912a6c0402f7602",
    ),
    ("linux", "cp312"): Pin(
        "https://files.pythonhosted.org/packages/ad/f7/7ce72cbf19d0addd05090b339152fd0548a02562c2866a603e6e3b3da2df/"
        f"librt-{LIBRT_VERSION}-cp312-cp312-manylinux2014_x86_64.manylinux_2_17_x86_64.manylinux_2_28_x86_64.whl",
        "36e53948e99bbe3ffea257124cfcae1cfb01831555c9a9c903c9f9a72db7fd07",
    ),
    ("linux", "cp313"): Pin(
        "https://files.pythonhosted.org/packages/a9/59/16c409c56f708eda2db9a0553662845d45e3871c77d70a240dae3f3bdc56/"
        f"librt-{LIBRT_VERSION}-cp313-cp313-manylinux2014_x86_64.manylinux_2_17_x86_64.manylinux_2_28_x86_64.whl",
        "17bac7f7a16b328fff77e440287693eb017abde913595b5827ebccbc21ecd8a6",
    ),
    ("linux", "cp314"): Pin(
        "https://files.pythonhosted.org/packages/50/07/79007d2165f649ea93e08c0962d1d62c70af9cde77965255095bf9d96f9a/"
        f"librt-{LIBRT_VERSION}-cp314-cp314-manylinux2014_x86_64.manylinux_2_17_x86_64.manylinux_2_28_x86_64.whl",
        "ca8052401c55d7511dda6760719fda7618067e83535d7d0010096d216c34b667",
    ),
    ("windows", "cp311"): Pin(
        "https://files.pythonhosted.org/packages/d1/38/c892383d2bb1a6a80b2ec8ede84970a265a792c56614a06282bf3008517c/"
        f"librt-{LIBRT_VERSION}-cp311-cp311-win_amd64.whl",
        "5f49cff01bd608ef7d97104cb035c75455e79c2d70bf4a506cf773338ac1860d",
    ),
    ("windows", "cp312"): Pin(
        "https://files.pythonhosted.org/packages/f0/2c/5193dc81127cd5ddfad031391b046bf32dda219b38463ab872407ca30646/"
        f"librt-{LIBRT_VERSION}-cp312-cp312-win_amd64.whl",
        "25a58a19ea8d83b68209f04912df765e9260635ef77646542ed4b4abe6bc7940",
    ),
    ("windows", "cp313"): Pin(
        "https://files.pythonhosted.org/packages/cf/24/80bbb463c60ed18e29cb26aba386ddb76609580e4e7160710e550587018c/"
        f"librt-{LIBRT_VERSION}-cp313-cp313-win_amd64.whl",
        "14ed6ebe3e4f85f326d7920011ad30ff49ed9334e62cf88caef9ba973d9e3a92",
    ),
    ("windows", "cp314"): Pin(
        "https://files.pythonhosted.org/packages/c2/98/769712f356a1e897df3581bb0c3100375d054a000de26099360ea65b5111/"
        f"librt-{LIBRT_VERSION}-cp314-cp314-win_amd64.whl",
        "3f0b8114c44b2ac06ff5dacd08e07e8e807ff4f46083f2a1602685122559be41",
    ),
}
AST_SERIALIZE_VERSION = "0.13.1"
AST_SERIALIZE = {
    "linux": Pin(
        "https://files.pythonhosted.org/packages/c5/fa/da311559dd3dae9af5825cf21660775a929b970f72e3786af3f2cd667944/"
        f"ast_serialize-{AST_SERIALIZE_VERSION}-cp39-abi3-manylinux_2_17_x86_64.manylinux2014_x86_64.whl",
        "9f99786e0d62082a5b121b3437f3167562133711df22128de1e9886f57f0b226",
    ),
    "windows": Pin(
        "https://files.pythonhosted.org/packages/9f/6f/bafd6490fe7b355627fabe95f4417e3d02da93102c8552550eb9a0774e1e/"
        f"ast_serialize-{AST_SERIALIZE_VERSION}-cp39-abi3-win_amd64.whl",
        "ddf7cbc9c8abf70102c4a088efe154bb86bfb825b0a10e556d2b15972cf99bd7",
    ),
}
MYPY_PURE = (
    Pin(
        "https://files.pythonhosted.org/packages/49/d3/b8441a820a491ddfc024b0b0cf0393375b75ea13866d9c66727e54c2fc80/"
        "typing_extensions-4.16.0-py3-none-any.whl",
        "481caa481374e813c1b176ada14e97f1f67a4539ce9cfeb3f350d78d6370c2e8",
    ),
    Pin(
        "https://files.pythonhosted.org/packages/b5/99/7b1adac7e97ce7bbda34792dc6e9b2f5cc5cffd1875883f78a818b2a2687/"
        "mypy_extensions-1.2.0-py3-none-any.whl",
        "993e7a20aa5671eb41a7132cf555af30062da99729601601b9a8382b11cae768",
    ),
    Pin(
        "https://files.pythonhosted.org/packages/f1/d9/7fb5aa316bc299258e68c73ba3bddbc499654a07f151cba08f6153988714/"
        "pathspec-1.1.1-py3-none-any.whl",
        "a00ce642f577bf7f473932318056212bc4f8bfdf53128c78bbd5af0b9b20b189",
    ),
)

# The tools an AppImage is made with, by the name each is kept under in its folder of out/deps/
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

# Clang 23, every Linux build's compiler and linker (`deps.llvm`): kernel.org's slim build of LLVM's release, made
# on Debian 10 so it runs on AlmaLinux 8's glibc 2.28, where LLVM's own Linux release asks a newer one. The digest is
# the one kernel.org signs in `sha256sums.asc` beside it. The set's clang-tidy port is built from the same release
# (`vcpkg/ports/clang-tidy`), which a test holds to this version.
LLVM_VERSION = "23.1.5"
LLVM = Pin(
    f"https://www.kernel.org/pub/tools/llvm/files/llvm-{LLVM_VERSION}-x86_64.tar.xz",
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
# vcpkg (`deps.vcpkg_root`): its registry at release 2026.10.16's commit, whose ports fix every library's version,
# and the tool's 2026-10-29 release, newer than the one the registry names; the set built with the two holds them
# together. The registry's digest is of GitHub's archive of the commit, the tools' those Microsoft's release lists.
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
    digest="51e66101f687c68e",
    sha256={
        "linux": ("8a21da9d52116ddd4ea88d8988ec533e7c234c7ced0c61a41e25d88df8b75d0e",),
        "windows": ("0e323bada98143edb66e39bd2ed9f474f0a4254679ece54a2bea2ad56dd9626f",
                    "0c03057d2d4c6a8470462a3411932ced32463628575f4d72b86d894e6da16e7c"),
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
