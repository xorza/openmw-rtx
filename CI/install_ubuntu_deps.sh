#!/bin/bash
# The packages a Linux build of the tree takes on Ubuntu 24.04, as root: upstream's list, plus Ninja,
# the system Google Test, the clang-format `omw build` checks with, Qt's Wayland platform and zsync for
# the archive, and the window systems and input devices of the SDL3 CMake builds, as Ubuntu 24.04 has
# no SDL3. One list for CI's runner (`.github/actions/openmw-deps`) and the container a desk builds a
# release in (`release-base.Dockerfile`).

set -euo pipefail

here="$(dirname "$(readlink -f "$0")")"

# **A connection silent for thirty seconds is given up**, every fetch the install makes, so a mirror
# that stops answering is left for the next one sooner: a stalled mirror held one install for 96
# minutes on 2026-10-22. A mirror that trickles is not caught here; what a stall costs in all is
# bounded where CI runs this (`.github/actions/openmw-deps`).
printf 'Acquire::http::Timeout "30";\nAcquire::https::Timeout "30";\n' >/etc/apt/apt.conf.d/99-silent-mirror

"$here/install_debian_deps.sh" gcc openmw-deps openmw-deps-dynamic

# A PPA whose index never came is ignored by apt, which then installs Ubuntu's own recastnavigation,
# MyGUI and OpenSceneGraph in place of the PPA's, and the configure step refuses them minutes later
# without naming the cause. Launchpad answers 503 for minutes at a time, and a job it failed is run
# again rather than held in retries.
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
