# The registry's port, built here on Linux: the registry's leaves libxkbcommon to the system there, an empty
# package, and the release base's is 0.9, whose keysym table predates the Compose files of a current host. Static,
# against the system's XCB, without the registry (`xkbregistry`), the tools and Wayland, which Qt reads none of.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO xkbcommon/libxkbcommon
    REF "xkbcommon-${VERSION}"
    SHA512 261af9e02ad533bb6560e2711171812c7d92094db8b54eaa19b477ab68270e6d62b315e18d42527ca9c944e87f36f063d003d74aeb4829b21ecb96ab1b0c1ce7
    HEAD_REF master
    PATCHES
        disable-test.patch
)

# Flex and Bison named to meson by path: the PATH it runs under puts Python's folder, `/usr/bin`, first, whose Bison
# AlmaLinux 8 ships at 3.0.4, where libxkbcommon asks 3.6 (`env/install_almalinux_deps.sh` builds one).
vcpkg_find_acquire_program(FLEX)
vcpkg_find_acquire_program(BISON)

# The host's keymaps and Compose files, where every distribution keeps them: a path inside the set would hold none.
vcpkg_configure_meson(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -Dxkb-config-root=/usr/share/X11/xkb
        -Dxkb-config-extra-path=/etc/xkb
        -Dx-locale-root=/usr/share/X11/locale
        -Denable-x11=true
        -Denable-xkbregistry=false
        -Denable-wayland=false
        -Denable-docs=false
        -Denable-tools=false
    ADDITIONAL_BINARIES
        "flex = ['${FLEX}']"
        "bison = ['${BISON}']"
)
vcpkg_install_meson()
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
