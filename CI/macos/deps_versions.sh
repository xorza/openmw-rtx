VCPKG_TAG='2026-09-17'
QT_VER='6.11.2'

# What the build targets, and so what a library built for it here targets too: a loader built for a
# newer macOS than the program is one the program cannot load where it says it runs.
MACOS_DEPLOYMENT_TARGET='14.8'

# Where the Vulkan SDK stands once `before_install.macos.sh` installed it, for the tools and the
# headers `before_script.macos.sh` names: the installer's root, under which the SDK keeps a folder a
# system.
VULKAN_SDK='/tmp/VulkanSDK/macOS'
