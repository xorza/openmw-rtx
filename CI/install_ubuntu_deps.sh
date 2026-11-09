#!/bin/bash
# One list for CI's runner and the release container (`release-base.Dockerfile`). Ubuntu 24.04 has
# no SDL3, so the SDL3 build takes its window systems and input devices from here.

set -euo pipefail

here="$(dirname "$(readlink -f "$0")")"

# A stalled mirror held one install for 96 minutes. A trickling mirror is bounded where CI runs
# this.
printf 'Acquire::http::Timeout "30";\nAcquire::https::Timeout "30";\n' >/etc/apt/apt.conf.d/99-silent-mirror

# The runner's list puts Azure's mirror first, which at times throttles a download to a crawl on a
# connection that stays open, so no timeout or retry of apt's leaves it. Canonical's own goes first,
# as Servo and Wasmtime have it (actions/runner-images#12949). The release container has no such list.
mirrors=/etc/apt/apt-mirrors.txt
if [ -f "$mirrors" ]; then
  sed -i '/archive\.ubuntu\.com\/ubuntu\/\tpriority/ s/priority:2/priority:0/' "$mirrors"
  cat "$mirrors"
  # An image that changed the list leaves Azure first, and the stalls come back with nothing to say why.
  if ! grep -qP '//archive\.ubuntu\.com/ubuntu/\tpriority:0$' "$mirrors"; then
    echo "::warning::$mirrors holds no line this script knows, so Azure's mirror stays first"
  fi
fi

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
# Into the folder `install_debian_deps.sh` downloads into, which CI keeps between runs.
cache="${PWD}/apt-cache"
apt-get -o dir::cache::archives="$cache" install -y ninja-build libgtest-dev libgmock-dev qt6-wayland zsync clang-format-14
apt-get -o dir::cache::archives="$cache" install -y libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
  libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev libdecor-0-dev libegl-dev libgl-dev libdrm-dev \
  libgbm-dev libdbus-1-dev libibus-1.0-dev libudev-dev
