#!/bin/bash
# One list for CI's runner and the release container (`release-base.Dockerfile`). Ubuntu 24.04 has
# no SDL3, so the SDL3 build takes its window systems and input devices from here.

set -euo pipefail

here="$(dirname "$(readlink -f "$0")")"

# A stalled mirror held one install for 96 minutes. A trickling mirror is bounded where CI runs
# this.
printf 'Acquire::http::Timeout "30";\nAcquire::https::Timeout "30";\n' >/etc/apt/apt.conf.d/99-silent-mirror

"$here/install_debian_deps.sh" gcc openmw-deps openmw-deps-dynamic

# Without a PPA's index, apt installs Ubuntu's own recastnavigation, MyGUI and OpenSceneGraph, which
# the configure step refuses minutes later without naming the cause.
for ppa in openmw openmw-daily staging; do
  if ! compgen -G "/var/lib/apt/lists/ppa.launchpadcontent.net_openmw_${ppa}_ubuntu_dists_*_InRelease" >/dev/null; then
    >&2 echo "error: no index of ppa:openmw/${ppa} came from Launchpad, so its packages were not installed"
    exit 1
  fi
done

export DEBIAN_FRONTEND=noninteractive
apt-get install -y ninja-build libgtest-dev libgmock-dev qt6-wayland zsync clang-format-14
apt-get install -y libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
  libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev libdecor-0-dev libegl-dev libgl-dev libdrm-dev \
  libgbm-dev libdbus-1-dev libibus-1.0-dev libudev-dev
