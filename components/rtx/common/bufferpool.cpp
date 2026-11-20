#include "bufferpool.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace Rtx
{
    PooledBuffer::PooledBuffer(
        BufferPool& pool, std::unique_ptr<std::byte[]> data, const std::size_t capacity, const std::size_t size)
        : mPool(&pool)
        , mData(std::move(data))
        , mCapacity(capacity)
        , mSize(size)
    {
    }

    PooledBuffer::PooledBuffer(PooledBuffer&& other) noexcept
        : mPool(std::exchange(other.mPool, nullptr))
        , mData(std::move(other.mData))
        , mCapacity(std::exchange(other.mCapacity, 0))
        , mSize(std::exchange(other.mSize, 0))
    {
    }

    PooledBuffer& PooledBuffer::operator=(PooledBuffer&& other) noexcept
    {
        if (this != &other)
        {
            if (mData != nullptr)
                mPool->give(std::move(mData), mCapacity);
            mPool = std::exchange(other.mPool, nullptr);
            mData = std::move(other.mData);
            mCapacity = std::exchange(other.mCapacity, 0);
            mSize = std::exchange(other.mSize, 0);
        }
        return *this;
    }

    void PooledBuffer::truncate(const std::size_t size)
    {
        assert(size <= mSize && "a buffer truncated past what it holds");
        mSize = size;
    }

    PooledBuffer::~PooledBuffer()
    {
        if (mData != nullptr)
            mPool->give(std::move(mData), mCapacity);
    }

    BufferPool::BufferPool(const std::size_t mostBytes)
        : mMostBytes(mostBytes)
    {
        assert(mostBytes >= sClassLeast && "a pool of less than its smallest class");
        // **Each class's free list made for every buffer of it the most holds, and one more**, so a buffer given back,
        // on whatever thread drops its holder, the frame's among them, grows nothing; the one more is a buffer made
        // past the most, alone or under an overdraft, which the next take drops.
        for (std::size_t at = 0; at < sClassCount; ++at)
            mFree[at].reserve(std::max<std::size_t>(1, mostBytes / (sClassLeast << at)) + 1);
    }

    BufferPool::~BufferPool()
    {
        assert(mInUse == 0 && "a pool outlived by a buffer it handed out");
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

    std::optional<PooledBuffer> BufferPool::take(const std::size_t bytes, const std::stop_token& stop)
    {
        const std::size_t capacity = classBytesOf(bytes);
        std::vector<std::unique_ptr<std::byte[]>>& own = mFree[classOf(capacity)];

        // **Whether a buffer may be made now**, the free ones of other classes dropped for it: under the most, or past
        // it where nothing else is in use, so a take larger than the pool is made and not waited on for ever, or while
        // an overdraft stands. One statement, which the take and its wait both read: stated twice, the wait's left
        // the pool standing empty out, and a take past the most that met a buffer in use waited for ever.
        const auto mayMake = [&] { return mInUse + capacity <= mMostBytes || mInUse == 0 || mOverdrafts > 0; };

        // **What is dropped is freed past the lock**, on this thread, which every thread that takes and gives waits on:
        // a take that drops makes a buffer or has the giving threads' excess to drop, which a list costs nothing past.
        std::vector<std::unique_ptr<std::byte[]>> dropped;
        std::unique_lock lock(mMutex);
        while (true)
        {
            // A kept buffer under the same word a new one is made on: one kept past the most is the next take's to
            // drop, and never more in use than the most allows.
            if (!own.empty() && mayMake())
            {
                std::unique_ptr<std::byte[]> data = std::move(own.back());
                own.pop_back();
                mFreeBytes -= capacity;
                mInUse += capacity;
                dropExcess(dropped);
                lock.unlock();
                return PooledBuffer(*this, std::move(data), capacity, bytes);
            }

            if (mayMake())
            {
                // Free buffers of other classes dropped, the largest first, until the new one fits under the most.
                mInUse += capacity;
                dropExcess(dropped);
                lock.unlock();
                return PooledBuffer(*this, std::make_unique_for_overwrite<std::byte[]>(capacity), capacity, bytes);
            }

            if (!mGiven.wait(lock, stop, mayMake))
                return std::nullopt;
        }
    }

    void BufferPool::give(std::unique_ptr<std::byte[]> data, const std::size_t capacity) noexcept
    {
        // Freed past the lock where it finds no room in its list, which every thread that takes and gives waits on.
        std::unique_ptr<std::byte[]> unkept;
        {
            const std::scoped_lock lock(mMutex);
            assert(mInUse >= capacity && "a buffer given back that the pool did not hand out");
            mInUse -= capacity;
            std::vector<std::unique_ptr<std::byte[]>>& own = mFree[classOf(capacity)];
            // **Kept, past the most too**, where its list has the room it was made with: the next take drops what
            // stands past the most, on its own thread. Every buffer of a class the most holds and one more find room,
            // so a buffer is freed here only past that.
            if (own.size() < own.capacity())
            {
                own.push_back(std::move(data));
                mFreeBytes += capacity;
            }
            else
                unkept = std::move(data);
        }
        mGiven.notify_all();
    }

    void BufferPool::dropExcess(std::vector<std::unique_ptr<std::byte[]>>& into)
    {
        for (std::size_t at = sClassCount; at-- > 0 && mInUse + mFreeBytes > mMostBytes;)
            while (!mFree[at].empty() && mInUse + mFreeBytes > mMostBytes)
            {
                into.push_back(std::move(mFree[at].back()));
                mFree[at].pop_back();
                mFreeBytes -= sClassLeast << at;
            }
    }

    BufferPool::Overdraft::Overdraft(BufferPool& pool)
        : mPool(pool)
    {
        {
            const std::scoped_lock lock(mPool.mMutex);
            ++mPool.mOverdrafts;
        }
        mPool.mGiven.notify_all();
    }

    BufferPool::Overdraft::~Overdraft()
    {
        const std::scoped_lock lock(mPool.mMutex);
        assert(mPool.mOverdrafts > 0 && "an overdraft ended that did not stand");
        --mPool.mOverdrafts;
    }

    PoolHeld BufferPool::getHeld() const
    {
        const std::scoped_lock lock(mMutex);
        return PoolHeld{ .mInUse = mInUse, .mFree = mFreeBytes };
    }
}
