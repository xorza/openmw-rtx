#!/usr/bin/env bash

set -xe

# Creating build directory...
mkdir -p build
cd build

# Running CMake...
cmake -DOPENMW_USE_SYSTEM_SDL3=OFF ../

# Building with $NPROC CPU...
make -j $NPROC
