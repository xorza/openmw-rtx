# Nothing built: OpenAL Soft compiles against the system's headers, which CI/install_almalinux_deps.sh
# installs, and opens the system's libasound at run time (ALSOFT_DLOPEN, its default).
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
