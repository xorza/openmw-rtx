#[=======================================================================[.rst:
FindVulkanHeaders
-----------------

The Vulkan headers, and nothing of the loader: no program links it, because volk loads it where the
renderer makes an instance (``components/rtxvulkan``). The contract of the package Vulkan-Headers
installs, so either answers ``find_package(VulkanHeaders)`` alike.

CMake's FindVulkan does the search and reads the version, under its own ``Vulkan_INCLUDE_DIR``, and
this takes the headers' half of its answer: FindVulkan reports nothing found without the loader's
import library, whatever it was asked for. Qt's ``FindWrapVulkanHeaders`` does the same.

Imported targets
^^^^^^^^^^^^^^^^

``Vulkan::Headers``

Result variables
^^^^^^^^^^^^^^^^

``VulkanHeaders_FOUND``
``VulkanHeaders_VERSION``
  ``VK_HEADER_VERSION_COMPLETE``'s major and minor, then ``VK_HEADER_VERSION``.
#]=======================================================================]

find_package(Vulkan QUIET)
set(VulkanHeaders_VERSION "${Vulkan_VERSION}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VulkanHeaders REQUIRED_VARS Vulkan_INCLUDE_DIR VERSION_VAR VulkanHeaders_VERSION)

if (VulkanHeaders_FOUND AND NOT TARGET Vulkan::Headers)
    add_library(Vulkan::Headers INTERFACE IMPORTED)
    set_target_properties(Vulkan::Headers PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${Vulkan_INCLUDE_DIR}")
endif()
