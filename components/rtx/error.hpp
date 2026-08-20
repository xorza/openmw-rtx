#ifndef OPENMW_COMPONENTS_RTX_ERROR_H
#define OPENMW_COMPONENTS_RTX_ERROR_H

#include <stdexcept>
#include <string_view>

#include <vulkan/vulkan_core.h>

namespace Rtx
{
    /// Anything that stops the ray tracing renderer starting or running.
    ///
    /// Vulkan failures belong to bring-up: a missing extension, an unsupported format, a device out
    /// of memory. Once a frame is recording, a non-success result means this code broke a contract,
    /// and it is still thrown rather than asserted so the caller can shut the renderer down and
    /// leave the game running.
    class Error : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    /// Name of a `VkResult` as it is spelled in the header, for messages.
    std::string_view resultName(VkResult result);

    /// Throws `Error` naming `call` and the result unless `result` is `VK_SUCCESS`.
    ///
    /// `VK_INCOMPLETE` is a failure here. Enumeration loops that can legitimately see it handle it
    /// before calling this.
    void checkVk(VkResult result, const char* call);
}

#endif
