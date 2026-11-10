#!/bin/bash
# The packages a Linux build needs on Ubuntu 26.04, the one list for CI's runner and the release
# container (`release-base.Dockerfile`). OpenSceneGraph comes from OpenMW's PPAs, everything else from
# the archive. SDL3 is built static, so a package carries it inside the binaries, and its build takes
# its window systems and input devices from here.

set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
# Where the packages are downloaded to, which CI keeps between runs.
cache="${PWD}/apt-cache"
mkdir -p "$cache"

# A stalled mirror held one install for 96 minutes. A trickling mirror is bounded where CI runs
# this.
printf 'Acquire::http::Timeout "30";\nAcquire::https::Timeout "30";\nAcquire::Retries "5";\n' \
  >/etc/apt/apt.conf.d/99-silent-mirror

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

apt_install() {
  apt-get -qq -o dir::cache::archives="$cache" install -y --no-install-recommends "$@"
}

apt-get update -qq
apt_install software-properties-common gnupg
# Launchpad at times refuses a key or an index for a moment; five tries, then the run stops.
for ppa in openmw openmw-daily staging; do
  for attempt in 1 2 3 4 5; do
    add-apt-repository -y "ppa:openmw/${ppa}" && break
    [ "$attempt" -lt 5 ] || exit 1
    sleep 10
  done
done

# Without a PPA's index, apt installs Ubuntu's own OpenSceneGraph, which the configure step refuses
# minutes later without naming the cause.
for ppa in openmw openmw-daily staging; do
  if ! compgen -G "/var/lib/apt/lists/ppa.launchpadcontent.net_openmw_${ppa}_ubuntu_dists_*_InRelease" >/dev/null; then
    >&2 echo "error: no index of ppa:openmw/${ppa} came from Launchpad, so its packages were not installed"
    exit 1
  fi
done

apt_install \
  binutils build-essential ca-certificates ccache curl git mold ninja-build pkg-config unzip zsync \
  libboost-iostreams-dev libboost-program-options-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev \
  qt6-base-dev qt6-svg-dev qt6-svg-plugins qt6-tools-dev qt6-tools-dev-tools qt6-wayland \
  libbullet-dev libgmock-dev libgtest-dev libicu-dev libluajit-5.1-dev liblz4-dev libmygui-dev libopenal-dev \
  libunshield-dev libyaml-cpp-dev \
  libopenscenegraph-dev libcollada-dom-dev libjpeg-dev libpng-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
  libxinerama-dev libxt-dev libxv-dev libxxf86vm-dev libxkbcommon-dev libwayland-dev libdecor-0-dev libegl-dev \
  libgl-dev libgles-dev libdrm-dev libgbm-dev libdbus-1-dev libibus-1.0-dev libudev-dev \
  libasound2-dev libpulse-dev libsndio-dev
