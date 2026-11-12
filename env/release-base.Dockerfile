# AlmaLinux 8, whose glibc 2.28 is the floor the release runs on and the dependency set is built against
# (`omw deps-set`), with GCC 15 from Red Hat's gcc-toolset, whose libstdc++ the pinned Clang builds against: the
# system CI's Linux jobs run in too.
FROM almalinux:8@sha256:2d0f2eb2a83cc09dbc2eb92fade4dbe8f709628149e81aa00aec70630406603b

COPY install_almalinux_deps.sh /tmp/
RUN /tmp/install_almalinux_deps.sh && dnf clean all && rm /tmp/install_almalinux_deps.sh \
    && alternatives --set python3 /usr/bin/python3.12

# What `/opt/rh/gcc-toolset-15/enable` sets, for every command the container runs.
ENV PATH=/opt/rh/gcc-toolset-15/root/usr/bin:/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin:/bin:/sbin \
    LD_LIBRARY_PATH=/opt/rh/gcc-toolset-15/root/usr/lib64:/opt/rh/gcc-toolset-15/root/usr/lib \
    PKG_CONFIG_PATH=/opt/rh/gcc-toolset-15/root/usr/lib64/pkgconfig
