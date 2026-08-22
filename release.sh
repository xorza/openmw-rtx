#!/usr/bin/env bash
# Builds openmw-rtxtool optimised and opens it on the ship at Seyda Neen, with no validation layers.
# Its own build directory, so it does not fight debug.sh over one set of objects. Extra arguments
# are passed to the tool: `./release.sh --view=balmora`.
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

cmake --build "$build" -j32 --target openmw-rtxtool

cd "$build"
exec ./openmw-rtxtool --validation=false "$@"
