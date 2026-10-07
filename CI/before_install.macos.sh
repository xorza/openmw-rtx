#!/bin/bash -e

DEPS_DIR="/tmp"

source ./CI/macos/deps_versions.sh

brew tap --repair
brew update --quiet

brew install curl p7zip
# The Vulkan backend compiles here though no Mac traces rays: glslc, the SPIR-V tools and the headers
# from the SDK `tools/omw/pins.py` pins, as the other systems take theirs, into `VULKAN_SDK`
# (`deps_versions.sh`). No loader: no program links it, because volk loads it at run time.
read -r SDK_URL SDK_SHA256 SDK_VERSION < <(python3 -c 'import sys; sys.path.insert(0, "tools"); from omw import pins
print(pins.VULKAN_SDK_MACOS.url, pins.VULKAN_SDK_MACOS.sha256, pins.VULKAN_SDK_WINDOWS_VERSION)')
curl -fsSL "$SDK_URL" -o "$DEPS_DIR/vulkansdk.zip"
echo "$SDK_SHA256  $DEPS_DIR/vulkansdk.zip" | shasum -a 256 -c -
unzip -q "$DEPS_DIR/vulkansdk.zip" -d "$DEPS_DIR/vulkansdk-installer"
"$DEPS_DIR/vulkansdk-installer/vulkansdk-macOS-$SDK_VERSION.app/Contents/MacOS/vulkansdk-macOS-$SDK_VERSION" \
    --root "${VULKAN_SDK%/macOS}" --accept-licenses --default-answer --confirm-command install

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
