#!/bin/bash
# The packages a Linux desk on Ubuntu 26.04 builds with: the compilers and the tools, and the development
# files of what the programs open at run time and so stay the system's (OpenGL, X11, Wayland, fontconfig,
# ALSA, PulseAudio, D-Bus), as `CI/install_almalinux_deps.sh` installs them for CI and the release. The
# libraries the tree links come from the vcpkg set (`vcpkg.json`), not from here.

set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get -qq install -y --no-install-recommends \
  binutils build-essential ca-certificates ccache curl git mold ninja-build pkg-config python3 unzip zsync \
  libgl-dev libegl-dev libgles-dev libgbm-dev libdrm-dev \
  libx11-dev libx11-xcb-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxinerama-dev \
  libxss-dev libxtst-dev libxrender-dev libxkbcommon-dev libxkbcommon-x11-dev \
  libxcb1-dev libxcb-cursor-dev libxcb-glx0-dev libxcb-icccm4-dev libxcb-image0-dev libxcb-keysyms1-dev \
  libxcb-randr0-dev libxcb-render-util0-dev libxcb-shape0-dev libxcb-shm0-dev libxcb-sync-dev \
  libxcb-xfixes0-dev libxcb-xinput-dev libxcb-xkb-dev \
  libwayland-dev wayland-protocols libfontconfig-dev \
  libasound2-dev libpulse-dev libdbus-1-dev libudev-dev
