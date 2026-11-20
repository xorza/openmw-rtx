#pragma once

#include <array>
#include <bit>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace Rtx
{
    class BufferPool;

    /// A buffer of a `BufferPool`, its bytes the pool's until it goes, when they go back to it: moved, never copied.
    class PooledBuffer
    {
    public:
        PooledBuffer() = default;
        PooledBuffer(PooledBuffer&& other) noexcept;
        PooledBuffer& operator=(PooledBuffer&& other) noexcept;
        PooledBuffer(const PooledBuffer&) = delete;
        PooledBuffer& operator=(const PooledBuffer&) = delete;
        ~PooledBuffer();

        /// The bytes asked for, at the front of the buffer's class.
        std::span<std::byte> bytes() { return { mData.get(), mSize }; }
        std::span<const std::byte> bytes() const { return { mData.get(), mSize }; }

        bool isEmpty() const { return mData == nullptr; }

        /// Keeps the first `size` bytes as what was asked for, of the ones it holds.
        void truncate(std::size_t size);

    private:
        friend class BufferPool;
        PooledBuffer(std::shared_ptr<BufferPool> pool, std::unique_ptr<std::byte[]> data, std::size_t capacity,
            std::size_t size);

        /// Shared, since a buffer goes back to its pool from whatever holds it last, a texture slot's made image
        /// among them, which may outlive the pool's owner.
        std::shared_ptr<BufferPool> mPool;
        std::unique_ptr<std::byte[]> mData;
        std::size_t mCapacity = 0;
        std::size_t mSize = 0;
    };

    /// What a pool holds: its buffers handed out, and those it keeps free for the next take, in bytes of their
    /// classes.
    struct PoolHeld
    {
        std::size_t mInUse = 0;
        std::size_t mFree = 0;
    };

    /// **The buffers a texture's bytes are read into, bounded in bytes and by size class** (`docs/architecture.md`,
    /// "A texture's levels are read by position, into a buffer the reader owns"): a take reuses a free buffer of its
    /// class, and a buffer given back is kept for the next, so a hand-over allocates nothing once the classes a run
    /// asks are warm, as DirectStorage reads into a bounded staging buffer. What it holds, in use and free, stays
    /// under its most: a take past it drops free buffers of other classes, and waits for one given back where every
    /// byte is in use, a worker's wait and never the frame's thread. One buffer larger than the most is made where
    /// nothing else is in use, so no take waits for ever. Shared by every thread that takes and gives.
    class BufferPool : public std::enable_shared_from_this<BufferPool>
    {
    public:
        /// The smallest class, 64 KiB, under which a buffer is not worth a class of its own, and the largest, 1 GiB, a
        /// stored payload's most (`ContentStore::Store::sPayloadMost`).
        static constexpr std::size_t sClassLeast = std::size_t{ 1 } << 16;
        static constexpr std::size_t sClassMost = std::size_t{ 1 } << 30;
        static constexpr std::size_t sClassCount = std::bit_width(sClassMost) - std::bit_width(sClassLeast) + 1;

        /// Made shared, as each buffer it hands out holds it (`PooledBuffer`).
        static std::shared_ptr<BufferPool> make(std::size_t mostBytes);
        ~BufferPool();

        BufferPool(const BufferPool&) = delete;
        BufferPool& operator=(const BufferPool&) = delete;

        /// A buffer of `bytes`, at most `sClassMost`, waiting while every byte of the most is in use and no
        /// `Overdraft` stands; nothing where `stop` asks before one is given back.
        std::optional<PooledBuffer> take(std::size_t bytes, const std::stop_token& stop);

        /// **Every take past the most while it stands**, for a waiter that holds whatever it waits for until it has
        /// it all and gives back nothing meanwhile: a settled hand-over, which stands an arrival's every chain at once
        /// (`TextureHands::flush`). Under the most, its workers would wait for buffers only the waiter gives back,
        /// and the waiter for them. What it lets past is what that waiter asked, bounded by what it holds.
        class [[nodiscard]] Overdraft
        {
        public:
            explicit Overdraft(BufferPool& pool);
            ~Overdraft();

            Overdraft(const Overdraft&) = delete;
            Overdraft& operator=(const Overdraft&) = delete;

        private:
            BufferPool& mPool;
        };

        PoolHeld getHeld() const;

        /// The class a take of `bytes` is made in: the power of two at or past it, `sClassLeast` at the least.
        static std::size_t classBytesOf(std::size_t bytes);

    private:
        friend class PooledBuffer;

        /// Takes back what a buffer of `capacity` held, keeping it free for its class.
        void give(std::unique_ptr<std::byte[]> data, std::size_t capacity) noexcept;

        static std::size_t classOf(std::size_t capacity);

        explicit BufferPool(std::size_t mostBytes);

        const std::size_t mMostBytes;
        mutable std::mutex mMutex;
        std::condition_variable_any mGiven;
        std::array<std::vector<std::unique_ptr<std::byte[]>>, sClassCount> mFree;
        std::size_t mInUse = 0;
        std::size_t mFreeBytes = 0;

        /// How many `Overdraft`s stand.
        std::uint32_t mOverdrafts = 0;
    };
}
