# The system a Linux release is built on, for a desk that runs another: Ubuntu 24.04, CI's runner,
# whose glibc and libraries the AppImage takes as the floor it runs on. `omw archive` builds and runs
# it, and hands in the CMake `tools/omw/pins.py` pins.
FROM ubuntu:24.04@sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55

# What the runner image carries beyond a bare Ubuntu and the build reads: Python for `omw`, Git, the
# fetchers and their archives, and a CMake at the tree's floor, which Ubuntu's own 3.28 is under.
ARG CMAKE_URL
ARG CMAKE_SHA256
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        python3 git curl ca-certificates xz-utils unzip file \
    && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL "$CMAKE_URL" -o /tmp/cmake.tar.gz \
    && echo "$CMAKE_SHA256  /tmp/cmake.tar.gz" | sha256sum -c - \
    && mkdir /opt/cmake \
    && tar -xzf /tmp/cmake.tar.gz -C /opt/cmake --strip-components=1 \
    && ln -s /opt/cmake/bin/* /usr/local/bin/ \
    && rm /tmp/cmake.tar.gz

WORKDIR /tmp/packages
COPY install_debian_deps.sh install_ubuntu_deps.sh ./
RUN ./install_ubuntu_deps.sh && rm -rf /tmp/packages /var/lib/apt/lists/*
WORKDIR /
