#include "hostwritten.hpp"

namespace Rtx::Testing
{
    Buffer hostWritten(CommandPool& pool, const std::span<const std::byte> bytes, const VkBufferUsageFlags usage)
    {
        Buffer made = Buffer::hostWritten(pool.getDevice(), bytes.size(), usage, "test");
        flushWriting(pool, [&](Uploads& uploads) { uploads.write(made, 0, bytes); });
        return made;
    }
}
