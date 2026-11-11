# Nothing built: Qt links the system's libfontconfig, whose development files
# CI/install_almalinux_deps.sh installs.
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
