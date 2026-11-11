#!/bin/bash
# The packages an AlmaLinux 8 build needs, in CI's containers and the release image alike: GCC 15 from
# Red Hat's gcc-toolset, the tools vcpkg's ports and the tree build with (mold and ccache the tree's), and
# the development files of what the programs open at run time and so stay the system's (OpenGL, X11,
# Wayland, fontconfig, ALSA, PulseAudio, D-Bus).
# The libraries the tree links come from the vcpkg set (`vcpkg.json`), not from here.
set -euo pipefail

dnf install -y epel-release dnf-plugins-core
dnf config-manager --set-enabled powertools
dnf install -y \
  gcc-toolset-15 gcc-toolset-15-libatomic-devel \
  git curl zip unzip tar xz bzip2 patch which file \
  python3.12 perl perl-IPC-Cmd perl-Time-Piece \
  ccache mold ninja-build make pkgconfig autoconf automake libtool autoconf-archive gettext-devel texinfo nasm bison flex \
  kernel-headers \
  mesa-libGL-devel mesa-libEGL-devel mesa-libgbm-devel libglvnd-devel \
  libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel libXinerama-devel libXScrnSaver-devel \
  libXfixes-devel libXrender-devel libXft-devel libXtst-devel libXdamage-devel libXcomposite-devel \
  libxcb-devel xcb-util-devel xcb-util-image-devel xcb-util-keysyms-devel xcb-util-renderutil-devel \
  xcb-util-wm-devel xcb-util-cursor-devel libxkbcommon-devel libxkbcommon-x11-devel \
  wayland-devel wayland-protocols-devel \
  fontconfig-devel alsa-lib-devel pulseaudio-libs-devel dbus-devel systemd-devel libdrm-devel

# Autoconf 2.72, which vcpkg's autotools ports ask for and AlmaLinux 8 ships at 2.69, as upstream's
# dependency builder does it.
if ! autoconf --version | grep -q " 2\.72$"; then
  work="$(mktemp -d)"
  curl -fsSL https://ftp.gnu.org/gnu/autoconf/autoconf-2.72.tar.gz -o "$work/autoconf.tar.gz"
  echo "3f71d459e2127ef2c98b37f11f5284f0c92318ad8d31c5b6034bed370adfe9f3d0f7a12f87e2efced06c11e2a159de4d4a292b751104e83daf11342f25ba9721  $work/autoconf.tar.gz" | sha512sum -c -
  tar xf "$work/autoconf.tar.gz" -C "$work"
  (cd "$work/autoconf-2.72" && ./configure --prefix=/usr/local && make -j"$(nproc)" && make install)
  rm -rf "$work"
fi
