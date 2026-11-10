"""Every download the driver pins, by version and SHA-256: the one file a bump edits, and what CI
keys its cache of deps/ on beside upstream's `CI/deps_versions.msvc.sh`, so a bump saves the cache
afresh and an edit to the code around the pins does not. Why each is pinned the way it is stays with
the code that fetches it, in `deps.py`."""

from dataclasses import dataclass


@dataclass(frozen=True)
class Pin:
    url: str
    sha256: str


# aqt, which installs Qt on Windows for the flavours that build the Qt programs.
AQT = Pin(
    "https://github.com/miurahr/aqtinstall/releases/download/v3.1.15/aqt_x64.exe",
    "f9e9acc05975f2e70e4935ace76b41882cda4b53eb779c9bcf1d87a95029a6b0",
)

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

# The tools an AppImage is made with, by the name each is kept under in its folder of deps/
# (`deps.appimage_tools`).
APPIMAGE_TOOLS = {
    "linuxdeploy": Pin(
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage",
        "c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d",
    ),
    "linuxdeploy-plugin-qt": Pin(
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage",
        "15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724",
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

# The Vulkan SDK from LunarG, each system's with the checksum LunarG publishes beside the file in its
# own listing, sdk.lunarg.com/sdk/files.json. On Windows the loader comes apart from the SDK, from the
# runtime components LunarG publishes for redistribution.
VULKAN_SDK_LINUX_VERSION = "1.4.357.1"
VULKAN_SDK_LINUX = Pin(
    f"https://sdk.lunarg.com/sdk/download/{VULKAN_SDK_LINUX_VERSION}/linux/vulkansdk-linux-x86_64-{VULKAN_SDK_LINUX_VERSION}.tar.xz",
    "4b41e3b30e8aedaa5dac7c136561ab463eb316a25a54e2c6245f2c299ea1fb85",
)
VULKAN_SDK_WINDOWS_VERSION = "1.4.357.0"
VULKAN_SDK_WINDOWS = Pin(
    f"https://sdk.lunarg.com/sdk/download/{VULKAN_SDK_WINDOWS_VERSION}/windows/vulkansdk-windows-X64-{VULKAN_SDK_WINDOWS_VERSION}.exe",
    "81f474711e9042f4cd22b31b2f7a8870db2e428b21586fb43dd80150be97310d",
)
VULKAN_LOADER_WINDOWS = Pin(
    f"https://sdk.lunarg.com/sdk/download/{VULKAN_SDK_WINDOWS_VERSION}/windows/VulkanRT-X64-{VULKAN_SDK_WINDOWS_VERSION}-Components.zip",
    "a14672efed15aafc7f5a16572d35cd3a3416eadf670aeee3cdf50ee32d5fbf83",
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
