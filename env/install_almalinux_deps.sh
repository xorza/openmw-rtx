#!/bin/bash
# The packages an AlmaLinux 8 build needs, in CI's containers and the release image alike: GCC 15 from
# Red Hat's gcc-toolset with its sanitizers' runtimes, the tools vcpkg's ports and the tree build with (mold and ccache the tree's), and
# the development files of what the programs open at run time and so stay the system's (OpenGL, X11,
# Wayland, fontconfig, ALSA, PulseAudio, D-Bus).
# The libraries the tree links come from the vcpkg set (`vcpkg.json`), not from here.
set -euo pipefail

dnf install -y epel-release dnf-plugins-core
dnf config-manager --set-enabled powertools
dnf install -y \
  gcc-toolset-15 gcc-toolset-15-libatomic-devel \
  gcc-toolset-15-libasan-devel gcc-toolset-15-libubsan-devel gcc-toolset-15-libtsan-devel \
  git curl zip unzip tar xz bzip2 patch which file \
  python3.12 perl perl-IPC-Cmd perl-Time-Piece \
  ccache mold ninja-build meson make pkgconfig autoconf automake libtool autoconf-archive gettext-devel texinfo nasm bison flex \
  kernel-headers \
  mesa-libGL-devel mesa-libEGL-devel mesa-libgbm-devel libglvnd-devel \
  libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel libXinerama-devel libXScrnSaver-devel \
  libXfixes-devel libXrender-devel libXft-devel libXtst-devel libXdamage-devel libXcomposite-devel \
  libxcb-devel xcb-util-devel xcb-util-image-devel xcb-util-keysyms-devel xcb-util-renderutil-devel \
  xcb-util-wm-devel xcb-util-cursor-devel libxkbcommon-devel libxkbcommon-x11-devel \
  wayland-devel wayland-protocols-devel expat-devel libxml2-devel \
  fontconfig-devel alsa-lib-devel pulseaudio-libs-devel dbus-devel systemd-devel libdrm-devel

# `python3` is 3.12, which vcpkg's ports run (its meson asks for 3.7 or later): the meson package brings Python
# 3.6, whose alternative outranks 3.12's. That meson runs on the platform's own Python, which this leaves be.
alternatives --set python3 /usr/bin/python3.12

# The tools below build with the GCC the set and the tree build with: AlmaLinux 8's own 8.5 is here only
# because libtool's package asks for it. The image's `PATH` names the toolset only after this script ran.
source /opt/rh/gcc-toolset-15/enable

# A download tried five times: a mirror refuses a runner's connection at times, and AlmaLinux 8's curl
# retries no refused connection (`--retry-all-errors` is 7.71's).
fetch() {
  for attempt in 1 2 3 4 5; do
    curl -fsSL "$1" -o "$2" && return
    [ "$attempt" -lt 5 ] && sleep 10
  done
  return 1
}

# Autoconf 2.72, which vcpkg's autotools ports ask for and AlmaLinux 8 ships at 2.69, as upstream's
# dependency builder does it.
if ! autoconf --version | grep -q " 2\.72$"; then
  work="$(mktemp -d)"
  fetch https://ftp.gnu.org/gnu/autoconf/autoconf-2.72.tar.gz "$work/autoconf.tar.gz"
  echo "3f71d459e2127ef2c98b37f11f5284f0c92318ad8d31c5b6034bed370adfe9f3d0f7a12f87e2efced06c11e2a159de4d4a292b751104e83daf11342f25ba9721  $work/autoconf.tar.gz" | sha512sum -c -
  tar xf "$work/autoconf.tar.gz" -C "$work"
  (cd "$work/autoconf-2.72" && ./configure --prefix=/usr/local && make -j"$(nproc)" && make install)
  rm -rf "$work"
fi

# zsync 0.6.2, whose `zsyncmake` writes the .zsync an AppImage's update information names, and which
# AlmaLinux 8 and EPEL do not carry: its release as Debian's mirror keeps it, as zsync's own site refused
# CI's runners. Its C predates the pointer checks GCC 14 made errors, which `-fpermissive` makes warnings
# again; `-w` keeps them, and the rest of its warnings, out of the log of a tool the build only runs.
if ! command -v zsyncmake > /dev/null; then
  work="$(mktemp -d)"
  fetch https://deb.debian.org/debian/pool/main/z/zsync/zsync_0.6.2.orig.tar.bz2 "$work/zsync.tar.bz2"
  echo "56437594831f2320c532759805b5c51116c53c8848f14b54cfd9b76676d1d061abe7fc914264f496ed1c7922fbbd5b884166c855a2fb9eb1285f216e478c5f0d  $work/zsync.tar.bz2" | sha512sum -c -
  tar xf "$work/zsync.tar.bz2" -C "$work"
  (cd "$work/zsync-0.6.2" && ./configure --prefix=/usr/local CFLAGS="-O2 -fpermissive -w" && make -j"$(nproc)" && make install)
  rm -rf "$work"
fi

# wayland-scanner 1.24.0, which SDL3 generates its Wayland code with: AlmaLinux 8's 1.21 predates the
# `deprecated-since` attribute its protocols carry, which wayland 1.23's DTD names, and warns on every file.
# The scanner alone, as only the generator changes and the programs keep the system's libwayland. Its
# release as wayland's GitLab publishes it, at the digest Arch's package checks.
if ! wayland-scanner --version 2>&1 | grep -q " 1\.24\.0$"; then
  work="$(mktemp -d)"
  fetch https://gitlab.freedesktop.org/wayland/wayland/-/releases/1.24.0/downloads/wayland-1.24.0.tar.xz "$work/wayland.tar.xz"
  echo "60216602cffd1473ae15381f9e9a6f3829e742cac83843ca65360ba78bcff8d9442f15eb6a6d5d29c8f74dd920762ca1605f51658d22a1d3c18e338505b9f974  $work/wayland.tar.xz" | sha512sum -c -
  tar xf "$work/wayland.tar.xz" -C "$work"
  (cd "$work/wayland-1.24.0" \
    && meson setup build --prefix=/usr/local -Dlibraries=false -Ddocumentation=false -Dtests=false \
    && meson install -C build)
  rm -rf "$work"
fi
