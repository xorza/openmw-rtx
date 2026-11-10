#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/common/index.hpp>
#include <components/rtxvulkan/device/commands.hpp>

#include "buffer.hpp"

namespace Rtx
{
    /// Appends `run` to `into`, as part of the last run where it carries that run on in both what it
    /// is written from and where it lands, and nothing for a run of no bytes.
    void appendRun(std::vector<CopyRun>& into, const CopyRun& run);

    /// Appends the runs `rows` make in a table of `stride` bytes a row, written from rows laid out as
    /// the table is: one a row, and one for each stretch of adjacent rows. `rows` ascending and each
    /// once, as a sort leaves a debt.
    void appendRowRuns(std::span<const Index> rows, VkDeviceSize stride, std::vector<CopyRun>& into);

    /// The one way the host writes a host-written buffer (`BufferKind::HostWritten`): staged into
    /// `batch`, with the copy recorded there (`docs/rtx/architecture.md`, "Host writes are staged").
    ///
    /// **What a write is read by is recorded after `order`**: into `batch` itself, or into a
    /// recording `batch` is deferred ahead of (`Batch::defer`), which the pool submits first.
    class Uploads
    {
    public:
        explicit Uploads(Batch& batch)
            : mBatch(batch)
        {
        }

        /// Asserts that nothing staged was left unordered, unless an exception is unwinding.
        ~Uploads();

        Uploads(const Uploads&) = delete;
        Uploads& operator=(const Uploads&) = delete;

        /// Writes `data` at `offset` bytes into `into`. Nothing at all for nothing.
        template <class T>
        void write(const Buffer& into, VkDeviceSize offset, std::span<const T> data)
        {
            writeBytes(into, offset, std::as_bytes(data));
        }

        /// Writes `runs` of `source` into `into`, each where it says, in one copy command.
        void writeRuns(const Buffer& into, std::span<const std::byte> source, std::span<const CopyRun> runs);

        /// `size` bytes landing at `offset` in `into`, lent for a caller that produces them in place:
        /// staging whose copy is recorded here. Written before the recording that reads it is
        /// submitted, and never read: it is write-combined memory.
        std::span<std::byte> lend(const Buffer& into, VkDeviceSize offset, VkDeviceSize size);

        /// Makes what was written so far readable by whatever `batch` records next: the barrier staged
        /// copies end in, and nothing where nothing was staged since the last.
        void order();

    private:
        void writeBytes(const Buffer& into, VkDeviceSize offset, std::span<const std::byte> bytes);

        Batch& mBatch;

        /// Whether a copy was recorded since the last `order`.
        bool mUnordered = false;
    };
}
