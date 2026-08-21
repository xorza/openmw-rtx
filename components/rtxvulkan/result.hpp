#pragma once

#include <string_view>

#include <vulkan/vulkan_core.h>

namespace Rtx
{
    /// Name of a `VkResult` as it is spelled in the header, for messages.
    std::string_view resultName(VkResult result);

    /// Throws `Error` naming `call` and the result unless `result` is `VK_SUCCESS`.
    ///
    /// `VK_INCOMPLETE` is a failure here. Enumeration loops that can legitimately see it handle it
    /// before calling this.
    void checkVk(VkResult result, const char* call);
}
