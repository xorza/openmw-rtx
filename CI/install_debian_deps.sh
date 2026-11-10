#!/bin/bash

set -euo pipefail

print_help() {
  echo "usage: $0 [group]..."
  echo
  echo "  available groups: "${!GROUPED_DEPS[@]}""
}

declare -rA GROUPED_DEPS=(
  [gcc]="binutils gcc build-essential ccache curl unzip git pkg-config mold"

  # Common dependencies for building OpenMW.
  [openmw-deps]="
    libboost-program-options-dev
    libboost-system-dev libboost-iostreams-dev

    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev
    libsdl2-dev libqt6opengl6-dev qt6-tools-dev qt6-tools-dev-tools libopenal-dev
    libunshield-dev libtinyxml-dev libbullet-dev liblz4-dev libpng-dev libjpeg-dev
    libluajit-5.1-dev ca-certificates libicu-dev
    libyaml-cpp-dev libqt6svg6 libqt6svg6-dev
    libmygui-dev libopenscenegraph-dev libcollada-dom-dev
  "
)

if [[ $# -eq 0 ]]; then
  >&2 print_help
  exit 1
fi

deps=()
for group in "$@"; do
  if [[ ! -v GROUPED_DEPS[$group] ]]; then
    >&2 echo "error: unknown group ${group}"
    exit 1
  fi
  deps+=(${GROUPED_DEPS[$group]})
done

export APT_CACHE_DIR="${PWD}/apt-cache"
export DEBIAN_FRONTEND=noninteractive
set -x
mkdir -pv "$APT_CACHE_DIR"

while true; do
  apt-get update -yqq && break
done

apt-get -qq -o dir::cache::archives="$APT_CACHE_DIR" install -y --no-install-recommends software-properties-common gnupg >/dev/null

while true; do
  add-apt-repository -y ppa:openmw/openmw && break
done

while true; do
  add-apt-repository -y ppa:openmw/openmw-daily && break
done

while true; do
  add-apt-repository -y ppa:openmw/staging && break
done

apt-get -qq -o dir::cache::archives="$APT_CACHE_DIR" install -y --no-install-recommends "${deps[@]}" >/dev/null
apt list --installed
