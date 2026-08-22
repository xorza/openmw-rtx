#!/usr/bin/env bash
# Builds optimised and opens the harness on the ship at Seyda Neen, with no validation layers. Its
# own build directory, so it does not fight debug.sh over one set of objects. Extra arguments are
# passed to the tool: `./release.sh --view=balmora`.
#
# `./release.sh game` builds and runs OpenMW itself on the quicksave instead, and
# `./release.sh bench` profiles — which is what an optimised build with no layers in it is for.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build="$root/build-release"

if [ ! -f "$build/CMakeCache.txt" ]; then
    cmake -S "$root" -B "$build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DOPENMW_RTX=ON \
        -DOPENMW_DLSS_SDK=/home/xxorza/Projects/rtxmw/.refs/dlss \
        -DBUILD_COMPONENTS_TESTS=OFF -DBUILD_OPENMW_TESTS=OFF \
        -DBUILD_OPENCS=OFF -DBUILD_WIZARD=OFF -DBUILD_ESSIMPORTER=OFF \
        -DBUILD_MWINIIMPORTER=OFF -DBUILD_OPENCS_TESTS=OFF \
        -DOPENMW_USE_SYSTEM_RECASTNAVIGATION=ON -DOPENMW_USE_SYSTEM_GOOGLETEST=ON \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=mold
fi

# `game` runs OpenMW itself on the quicksave; anything else goes to the harness. Two entry points
# rather than two more scripts, because the build directory and its configure line are the whole of
# what the two share and the whole of what makes them different from each other.
if [ "${1-}" = game ]; then
    shift
    cmake --build "$build" -j32 --target openmw
    cd "$build"
    exec ./openmw --skip-menu --load-savegame "$HOME/.local/share/openmw/saves/asd/Quicksave.omwsave" "$@"
fi

cmake --build "$build" -j32 --target openmw-rtxtool

# **The verb stays first.** `dispatch` reads it off argv[1] and takes a leading dash to mean nobody
# named one, so appending it after the switches below silently ran `view` instead — `./release.sh
# bench` opened a window and profiled nothing.
verb=()
if [ $# -gt 0 ] && [[ "${1}" != -* ]]; then
    verb=("$1")
    shift
fi

cd "$build"
exec ./openmw-rtxtool "${verb[@]}" --validation=false "$@"
