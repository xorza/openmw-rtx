#!/bin/sh -ex

cd build

make -j $(sysctl -n hw.logicalcpu)
