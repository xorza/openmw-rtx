"""Every download the driver pins, by version and SHA-256: the one file a bump edits, and what CI
keys its cache of deps/ on beside upstream's `CI/deps_versions.msvc.sh`, so a bump saves the cache
afresh and an edit to the code around the pins does not. Why each is pinned the way it is stays with
the code that fetches it, in `deps.py`."""

from dataclasses import dataclass


@dataclass(frozen=True)
class Pin:
    url: str
    sha256: str


# SDL's own Windows package. Its version is also the one upstream's set carries, and the Windows
# preset names the package's directory by it through `RTX_SDL2_VERSION`.
SDL2_VERSION = "2.32.10"
SDL2 = Pin(
    f"https://github.com/libsdl-org/SDL/releases/download/release-{SDL2_VERSION}/SDL2-devel-{SDL2_VERSION}-VC.zip",
    "af347939395a58b365846aaea27391e69f9ec9d4dd650d6ac40802159b418a6e",
)

# aqt, which installs Qt on Windows for the flavours that build the Qt programs.
AQT = Pin(
    "https://github.com/miurahr/aqtinstall/releases/download/v3.1.15/aqt_x64.exe",
    "f9e9acc05975f2e70e4935ace76b41882cda4b53eb779c9bcf1d87a95029a6b0",
)

# LLVM's Windows package, for the clang-format CI pins.
LLVM_RELEASE = "14.0.6"
LLVM = Pin(
    f"https://github.com/llvm/llvm-project/releases/download/llvmorg-{LLVM_RELEASE}/LLVM-{LLVM_RELEASE}-win64.exe",
    "e8dbb2f7de8e37915273d65c1c2f2d96844b96bb8e8035f62c5182475e80b9fc",
)

# The tools an AppImage is made with, by the name each is kept under in deps/appimage.
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
