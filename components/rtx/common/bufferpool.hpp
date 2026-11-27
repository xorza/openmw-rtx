#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace Rtx
{
    class BufferPool;
    class PooledBuffer;

    /// **Room in a `BufferPool` reserved for one buffer** before what fills it knows its size: the bytes of the most it
    /// may be, charged to the pool until the buffer it is spent on goes back, or until it goes unspent. Moved, never
    /// copied.
    class PoolCredit
    {
    public:
        PoolCredit() = default;
        PoolCredit(PoolCredit&& other) noexcept;
        PoolCredit& operator=(PoolCredit&& other) noexcept;
        PoolCredit(const PoolCredit&) = delete;
        PoolCredit& operator=(const PoolCredit&) = delete;
        ~PoolCredit();

        bool isEmpty() const { return mPool == nullptr; }

        /// The bytes charged: the class of the most reserved.
        std::size_t getCharged() const { return mCharged; }

        /// A buffer of `bytes`, at most what was reserved, in place of the credit, which carries the charge with it.
        /// Never waits: the room was the credit's from the start.
        PooledBuffer spend(std::size_t bytes) &&;

    private:
        friend class BufferPool;
        friend class PooledBuffer;
        PoolCredit(BufferPool& pool, std::size_t charged);

        BufferPool* mPool = nullptr;
        std::size_t mCharged = 0;
    };

    /// A buffer of a `BufferPool`, its bytes the pool's until it goes, when they go back to it with the charge of the
    /// credit it was spent from: moved, never copied.
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

        /// Gives the bytes back and keeps their charge, as the credit they were spent from: what a read that failed
        /// into them hands to the run that makes them instead.
        PoolCredit refund() &&;

    private:
        friend class BufferPool;
        friend class PoolCredit;
        PooledBuffer(BufferPool& pool, std::unique_ptr<std::byte[]> data, std::size_t capacity, std::size_t size,
            std::size_t charged);

        /// The pool it goes back to, which outlives every buffer it hands out by its owner's order: the renderer holds
        /// it ahead of everything that holds a buffer.
        BufferPool* mPool = nullptr;
        std::unique_ptr<std::byte[]> mData;
        std::size_t mCapacity = 0;
        std::size_t mSize = 0;
        std::size_t mCharged = 0;
    };

    /// What a pool holds: the credits and buffers it charged, and the buffers it keeps free for the next, in bytes of
    /// their classes.
    struct PoolHeld
    {
        std::size_t mCharged = 0;
        std::size_t mFree = 0;
    };

    /// **The buffers a texture's bytes are read into, bounded in bytes and by size class** (`docs/architecture.md`,
    /// "A made chain is handed over in a buffer of a bounded pool, read a segment at a time"): a buffer given back is
    /// kept free for the next of its class, so a hand-over allocates nothing once the classes a run asks are warm.
    ///
    /// **Reserved when the work is admitted, and never waited for** (`docs/architecture.md`, "A request is admitted
    /// with the room its texels take"): a credit is the room of the most a buffer may be, taken where the request is
    /// asked, and spent on the buffer once its size is known. What the credits and buffers charge stays under the most,
    /// but one past it made where nothing else is charged; a reserve that does not fit is refused, and nothing waits
    /// on a buffer given back, so no thread holding one can stand between another and the room it needs. **A buffer
    /// given back is kept and never freed on the giving thread**, the frame's among them: what stands past the most is
    /// dropped by the next spend, on the spender's thread and past the lock.
    class BufferPool
    {
    public:
        /// The smallest class, 64 KiB, under which a buffer is not worth a class of its own, and the largest, 1 GiB, a
        /// stored payload's most (`ContentStore::Store::sPayloadMost`).
        static constexpr std::size_t sClassLeast = std::size_t{ 1 } << 16;
        static constexpr std::size_t sClassMost = std::size_t{ 1 } << 30;
        static constexpr std::size_t sClassCount = std::bit_width(sClassMost) - std::bit_width(sClassLeast) + 1;

        explicit BufferPool(std::size_t mostBytes);

        /// Asserts that every credit and buffer it charged came back.
        ~BufferPool();

        BufferPool(const BufferPool&) = delete;
        BufferPool& operator=(const BufferPool&) = delete;

        /// Room for a buffer of `bytes` at most, at most `sClassMost`, where its class fits under the most beside what
        /// is charged, or nothing is; nothing where it does not.
        std::optional<PoolCredit> reserve(std::size_t bytes);

        PoolHeld getHeld() const;

        /// The class a buffer of `bytes` is made in: the power of two at or past it, `sClassLeast` at the least.
        static std::size_t classBytesOf(std::size_t bytes);

    private:
        friend class PoolCredit;
        friend class PooledBuffer;

        /// A buffer of `bytes` charged `charged`, which its credit reserved.
        PooledBuffer spend(std::size_t bytes, std::size_t charged);

        /// Takes back what a buffer of `capacity` charged `charged` held, keeping it free for its class.
        void give(std::unique_ptr<std::byte[]> data, std::size_t capacity, std::size_t charged) noexcept;

        /// Takes back what a buffer of `capacity` held and keeps its charge, for the credit it goes back to.
        void refund(std::unique_ptr<std::byte[]> data, std::size_t capacity) noexcept;

        /// Keeps `data` free for its class where its list has room, and hands it back to be freed past the lock
        /// otherwise. Under the lock.
        std::unique_ptr<std::byte[]> keepFree(std::unique_ptr<std::byte[]> data, std::size_t capacity) noexcept;

        /// Lets go of what a credit charged, unspent.
        void uncharge(std::size_t charged) noexcept;

        /// Moves into `into` the free buffers that stand past the most, the largest first, for the caller to free past
        /// the lock. Under the lock.
        void dropExcess(std::vector<std::unique_ptr<std::byte[]>>& into);

        static std::size_t classOf(std::size_t capacity);

        const std::size_t mMostBytes;
        mutable std::mutex mMutex;
        std::array<std::vector<std::unique_ptr<std::byte[]>>, sClassCount> mFree;
        std::size_t mCharged = 0;
        std::size_t mFreeBytes = 0;
    };
}
