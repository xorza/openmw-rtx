#!/bin/bash
# The packages a Linux build of the tree takes on Ubuntu 24.04, as root: upstream's list, plus Ninja,
# the system Google Test, the clang-format `omw build` checks with, Qt's Wayland platform and zsync for
# the archive, and the window systems and input devices of the SDL3 CMake builds, as Ubuntu 24.04 has
# no SDL3. One list for CI's runner (`.github/actions/openmw-deps`) and the container a desk builds a
# release in (`release-base.Dockerfile`).

set -euo pipefail

here="$(dirname "$(readlink -f "$0")")"

"$here/install_debian_deps.sh" gcc openmw-deps openmw-deps-dynamic
export DEBIAN_FRONTEND=noninteractive
apt-get install -y ninja-build libgtest-dev libgmock-dev qt6-wayland zsync clang-format-14
apt-get install -y libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
  libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev libdecor-0-dev libegl-dev libgl-dev libdrm-dev \
  libgbm-dev libdbus-1-dev libibus-1.0-dev libudev-dev
