# CI's runner, whose glibc and libraries are the floor the AppImage runs on.
FROM ubuntu:24.04@sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55

# CMake is the driver's own, which it fetches into deps/ (`deps.cmake`): Ubuntu's 3.28 is under the
# tree's floor.
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        python3 git curl ca-certificates xz-utils unzip file \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp/packages
COPY install_debian_deps.sh install_ubuntu_deps.sh ./
RUN ./install_ubuntu_deps.sh && rm -rf /tmp/packages /var/lib/apt/lists/*
WORKDIR /
