#!/bin/bash -e

DEPS_DIR="/tmp"

source ./CI/macos/deps_versions.sh

brew tap --repair
brew update --quiet

brew install curl p7zip
# The Vulkan backend compiles here though no Mac traces rays. glslc and the SPIR-V tools and headers
# from Homebrew; the Vulkan headers installed here, at the SDK `tools/omw/pins.py` pins for the other
# systems. No loader: no program links it, because volk loads it at run time.
brew install shaderc spirv-tools spirv-headers

VULKAN_SDK_VERSION=$(python3 -c 'import sys; sys.path.insert(0, "tools"); from omw import pins; print(pins.VULKAN_SDK_WINDOWS_VERSION)')
git -c advice.detachedHead=false clone --quiet --depth 1 --branch "vulkan-sdk-${VULKAN_SDK_VERSION}" \
    "https://github.com/KhronosGroup/Vulkan-Headers.git" "$DEPS_DIR/Vulkan-Headers"
cmake -S "$DEPS_DIR/Vulkan-Headers" -B "$DEPS_DIR/Vulkan-Headers/build" -D CMAKE_INSTALL_PREFIX="$DEPS_DIR/vulkan"
cmake --install "$DEPS_DIR/Vulkan-Headers/build"

pip install aqtinstall
aqt install-qt -O /tmp/Qt mac desktop $QT_VER && rm aqtinstall.log

curl "https://gitlab.com/OpenMW/openmw-deps/-/raw/main/macos/vcpkg-arm64-osx-dynamic-${VCPKG_TAG}-manifest.txt" -o $DEPS_DIR/openmw-manifest.txt

{ read -r URL && read -r HASH FILE; } < $DEPS_DIR/openmw-manifest.txt

curl -fSL -R -J $URL -o $DEPS_DIR/$FILE
echo "${HASH:?}  ${FILE:?}" | sha512sum
7z x -y -o$DEPS_DIR/openmw-deps-pre $DEPS_DIR/$FILE && \
    mv $DEPS_DIR/openmw-deps-pre/*/ $DEPS_DIR/openmw-deps/ && \
    rmdir $DEPS_DIR/openmw-deps-pre

command -v cmake >/dev/null 2>&1 || brew install cmake
