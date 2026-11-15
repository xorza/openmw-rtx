# LLVM's source release, the one archive every project of it is cut from.
vcpkg_download_distfile(
    ARCHIVE
    URLS "https://github.com/llvm/llvm-project/releases/download/llvmorg-${VERSION}/llvm-project-${VERSION}.src.tar.xz"
    FILENAME "llvm-project-${VERSION}.src.tar.xz"
    SHA512 fae39af451683ad864299f497167616e63d3a1bb5719f0d5123119e7398972cb602aef8cc271b8433910064afa88e1e7b8eede3fdd1ff58664c3189cf4ab5880
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")

vcpkg_find_acquire_program(PYTHON3)
string(REGEX MATCH "^[0-9]+" CLANG_VERSION_MAJOR "${VERSION}")

# The distribution's components alone are built, not LLVM's and Clang's every library and tool. The builtin headers
# stand beside the programs (`CLANG_RESOURCE_DIR`, from the program's folder), as the registry's llvm port lays
# them out, so the whole of `bin/` moves to the tools folder and clang-tidy still finds them. The host's target
# alone, which every compile it reads is for.
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/llvm"
    OPTIONS
        "-DLLVM_ENABLE_PROJECTS=clang;clang-tools-extra"
        "-DLLVM_DISTRIBUTION_COMPONENTS=clang-tidy;clang-apply-replacements;clang-resource-headers"
        -DLLVM_TARGETS_TO_BUILD=X86
        "-DCLANG_RESOURCE_DIR=lib/clang/${CLANG_VERSION_MAJOR}"
        -DLLVM_INCLUDE_TESTS=OFF
        -DLLVM_INCLUDE_BENCHMARKS=OFF
        -DLLVM_INCLUDE_EXAMPLES=OFF
        -DLLVM_INCLUDE_DOCS=OFF
        -DCLANG_INCLUDE_TESTS=OFF
        -DCLANG_INCLUDE_DOCS=OFF
        -DLLVM_ENABLE_ZLIB=OFF
        -DLLVM_ENABLE_ZSTD=OFF
        -DLLVM_ENABLE_LIBXML2=OFF
        -DLLVM_ENABLE_LIBEDIT=OFF
        -DLLVM_ENABLE_LIBPFM=OFF
        -DLLVM_ENABLE_TERMINFO=OFF
        "-DPython3_EXECUTABLE=${PYTHON3}"
)
vcpkg_cmake_build(TARGET install-distribution)

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/tools")
file(RENAME "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/tools/${PORT}")
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/llvm/LICENSE.TXT")
