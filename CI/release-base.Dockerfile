# Ubuntu 26.04, as CI's runners are: its glibc and libraries are the floor the AppImage runs on.
FROM ubuntu:26.04@sha256:6a06111c3b0a8600feadb270aec771dc239bf407b25625f93f44ab00e11f4d3c

# CMake is the driver's own, which it fetches into deps/ (`deps.cmake`): one CMake on every system.
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        python3 git curl ca-certificates xz-utils unzip file \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp/packages
COPY install_ubuntu_deps.sh ./
RUN ./install_ubuntu_deps.sh && rm -rf /tmp/packages /var/lib/apt/lists/*
WORKDIR /
