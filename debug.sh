#!/usr/bin/env bash
# Builds openmw-rtxtool with debug info and opens it on the ship at Seyda Neen, under the
# validation layers. Extra arguments are passed to the tool: `./debug.sh --view=balmora`.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build="$root/build"

# Configured once. `--clean-first` is never used here: it deletes files/lang/*.ts, which are source.
if [ ! -f "$build/CMakeCache.txt" ]; then
    cmake -S "$root" -B "$build" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DOPENMW_RTX=ON \
        -DOPENMW_DLSS_SDK=/home/xxorza/Projects/rtxmw/.refs/dlss \
        -DBUILD_COMPONENTS_TESTS=ON -DBUILD_OPENMW_TESTS=ON \
        -DBUILD_OPENCS=OFF -DBUILD_WIZARD=OFF -DBUILD_ESSIMPORTER=OFF \
        -DBUILD_MWINIIMPORTER=OFF -DBUILD_OPENCS_TESTS=OFF \
        -DOPENMW_USE_SYSTEM_RECASTNAVIGATION=ON -DOPENMW_USE_SYSTEM_GOOGLETEST=ON \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=mold
fi

cmake --build "$build" -j32 --target openmw-rtxtool

# From the build directory, because --resources defaults to ./resources.
cd "$build"
exec ./openmw-rtxtool --validation --sync-validation "$@"
