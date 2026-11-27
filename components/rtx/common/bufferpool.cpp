#include "bufferpool.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace Rtx
{
    PoolCredit::PoolCredit(BufferPool& pool, const std::size_t charged)
        : mPool(&pool)
        , mCharged(charged)
    {
    }

    PoolCredit::PoolCredit(PoolCredit&& other) noexcept
        : mPool(std::exchange(other.mPool, nullptr))
        , mCharged(std::exchange(other.mCharged, 0))
    {
    }

    PoolCredit& PoolCredit::operator=(PoolCredit&& other) noexcept
    {
        if (this != &other)
        {
            if (mPool != nullptr)
                mPool->uncharge(mCharged);
            mPool = std::exchange(other.mPool, nullptr);
            mCharged = std::exchange(other.mCharged, 0);
        }
        return *this;
    }

    PoolCredit::~PoolCredit()
    {
        if (mPool != nullptr)
            mPool->uncharge(mCharged);
    }

    PooledBuffer PoolCredit::spend(const std::size_t bytes) &&
    {
        assert(mPool != nullptr && "a credit spent twice, or never reserved");
        BufferPool& pool = *std::exchange(mPool, nullptr);
        return pool.spend(bytes, std::exchange(mCharged, 0));
    }

    PooledBuffer::PooledBuffer(BufferPool& pool, std::unique_ptr<std::byte[]> data, const std::size_t capacity,
        const std::size_t size, const std::size_t charged)
        : mPool(&pool)
        , mData(std::move(data))
        , mCapacity(capacity)
        , mSize(size)
        , mCharged(charged)
    {
    }

    PooledBuffer::PooledBuffer(PooledBuffer&& other) noexcept
        : mPool(std::exchange(other.mPool, nullptr))
        , mData(std::move(other.mData))
        , mCapacity(std::exchange(other.mCapacity, 0))
        , mSize(std::exchange(other.mSize, 0))
        , mCharged(std::exchange(other.mCharged, 0))
    {
    }

    PooledBuffer& PooledBuffer::operator=(PooledBuffer&& other) noexcept
    {
        if (this != &other)
        {
            if (mData != nullptr)
                mPool->give(std::move(mData), mCapacity, mCharged);
            mPool = std::exchange(other.mPool, nullptr);
            mData = std::move(other.mData);
            mCapacity = std::exchange(other.mCapacity, 0);
            mSize = std::exchange(other.mSize, 0);
            mCharged = std::exchange(other.mCharged, 0);
        }
        return *this;
    }

    void PooledBuffer::truncate(const std::size_t size)
    {
        assert(size <= mSize && "a buffer truncated past what it holds");
        mSize = size;
    }

    PoolCredit PooledBuffer::refund() &&
    {
        assert(mData != nullptr && "a refund of no buffer");
        BufferPool& pool = *std::exchange(mPool, nullptr);
        const std::size_t charged = std::exchange(mCharged, 0);
        pool.refund(std::move(mData), std::exchange(mCapacity, 0));
        mSize = 0;
        return PoolCredit(pool, charged);
    }

    PooledBuffer::~PooledBuffer()
    {
        if (mData != nullptr)
            mPool->give(std::move(mData), mCapacity, mCharged);
    }

    BufferPool::BufferPool(const std::size_t mostBytes)
        : mMostBytes(mostBytes)
    {
        assert(mostBytes >= sClassLeast && "a pool of less than its smallest class");
        // **Each class's free list made for every buffer of it the most holds, and one more**, so a buffer given back,
        // on whatever thread drops its holder, the frame's among them, grows nothing; the one more is a buffer made
        // past the most, alone, which the next spend drops.
        for (std::size_t at = 0; at < sClassCount; ++at)
            mFree[at].reserve(std::max<std::size_t>(1, mostBytes / (sClassLeast << at)) + 1);
    }

    BufferPool::~BufferPool()
    {
        assert(mCharged == 0 && "a pool outlived by a credit or a buffer it charged");
    }

    std::size_t BufferPool::classBytesOf(const std::size_t bytes)
    {
        assert(bytes <= sClassMost && "a buffer past the largest class");
        return std::max(sClassLeast, std::bit_ceil(bytes));
    }

    std::size_t BufferPool::classOf(const std::size_t capacity)
    {
        return static_cast<std::size_t>(std::bit_width(capacity) - std::bit_width(sClassLeast));
    }

    std::optional<PoolCredit> BufferPool::reserve(const std::size_t bytes)
    {
        const std::size_t charged = classBytesOf(bytes);
        const std::scoped_lock lock(mMutex);
        // **Under the most, or past it where nothing else is charged**, so a buffer larger than the pool is made and
        // not refused for ever.
        if (mCharged + charged > mMostBytes && mCharged != 0)
            return std::nullopt;
        mCharged += charged;
        return PoolCredit(*this, charged);
    }

    PooledBuffer BufferPool::spend(const std::size_t bytes, const std::size_t charged)
    {
        const std::size_t capacity = classBytesOf(bytes);
        assert(capacity <= charged && "a buffer past the credit it is spent from");

        // **What is dropped is freed past the lock**, on this thread, which every thread that spends and gives waits
        // on.
        std::vector<std::unique_ptr<std::byte[]>> dropped;
        std::unique_ptr<std::byte[]> data;
        {
            const std::scoped_lock lock(mMutex);
            std::vector<std::unique_ptr<std::byte[]>>& own = mFree[classOf(capacity)];
            if (!own.empty())
            {
                data = std::move(own.back());
                own.pop_back();
                mFreeBytes -= capacity;
            }
            // Free buffers of other classes dropped, the largest first, until what is charged and kept fits the most.
            dropExcess(dropped);
        }
        if (data == nullptr)
            data = std::make_unique_for_overwrite<std::byte[]>(capacity);
        return PooledBuffer(*this, std::move(data), capacity, bytes, charged);
    }

    void BufferPool::give(
        std::unique_ptr<std::byte[]> data, const std::size_t capacity, const std::size_t charged) noexcept
    {
        // Freed past the lock where it finds no room in its list.
        std::unique_ptr<std::byte[]> unkept;
        {
            const std::scoped_lock lock(mMutex);
            assert(mCharged >= charged && "a buffer given back that the pool did not charge");
            mCharged -= charged;
            unkept = keepFree(std::move(data), capacity);
        }
    }

    void BufferPool::refund(std::unique_ptr<std::byte[]> data, const std::size_t capacity) noexcept
    {
        std::unique_ptr<std::byte[]> unkept;
        {
            const std::scoped_lock lock(mMutex);
            unkept = keepFree(std::move(data), capacity);
        }
    }

    std::unique_ptr<std::byte[]> BufferPool::keepFree(
        std::unique_ptr<std::byte[]> data, const std::size_t capacity) noexcept
    {
        // **Kept, past the most too**, where its list has the room it was made with: the next spend drops what stands
        // past the most, on its own thread. Every buffer of a class the most holds and one more find room, so a buffer
        // is freed here only past that.
        std::vector<std::unique_ptr<std::byte[]>>& own = mFree[classOf(capacity)];
        if (own.size() >= own.capacity())
            return data;
        own.push_back(std::move(data));
        mFreeBytes += capacity;
        return nullptr;
    }

    void BufferPool::uncharge(const std::size_t charged) noexcept
    {
        const std::scoped_lock lock(mMutex);
        assert(mCharged >= charged && "a credit let go that the pool did not charge");
        mCharged -= charged;
    }

    void BufferPool::dropExcess(std::vector<std::unique_ptr<std::byte[]>>& into)
    {
        for (std::size_t at = sClassCount; at-- > 0 && mCharged + mFreeBytes > mMostBytes;)
            while (!mFree[at].empty() && mCharged + mFreeBytes > mMostBytes)
            {
                into.push_back(std::move(mFree[at].back()));
                mFree[at].pop_back();
                mFreeBytes -= sClassLeast << at;
            }
    }

    PoolHeld BufferPool::getHeld() const
    {
        const std::scoped_lock lock(mMutex);
        return PoolHeld{ .mCharged = mCharged, .mFree = mFreeBytes };
    }
}
