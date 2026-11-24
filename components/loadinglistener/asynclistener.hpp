#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>

#include "loadinglistener.hpp"

namespace Loading
{
    class AsyncListener : public Listener
    {
    public:
        explicit AsyncListener(Listener& baseListener)
            : mBaseListener(baseListener)
        {
        }

        void setLabel(const std::string& label, bool important) override
        {
            std::scoped_lock guard(mMutex);
            mLabelUpdate = label;
            mImportantLabel = important;
        }

        void setProgressRange(std::size_t range) override
        {
            std::scoped_lock guard(mMutex);
            mRangeUpdate = range;
            // The progress belongs to the previous range
            mProgressUpdate = std::nullopt;
        }

        void setProgress(std::size_t value) override
        {
            std::scoped_lock guard(mMutex);
            mProgressUpdate = value;
        }

        void increaseProgress(std::size_t increase) override { /* not implemented */ }

        void update()
        {
            std::scoped_lock guard(mMutex);
            if (mLabelUpdate)
                mBaseListener.setLabel(*mLabelUpdate, mImportantLabel);
            if (mRangeUpdate)
                mBaseListener.setProgressRange(*mRangeUpdate);
            if (mProgressUpdate)
                mBaseListener.setProgress(*mProgressUpdate);
            mLabelUpdate = std::nullopt;
            mRangeUpdate = std::nullopt;
            mProgressUpdate = std::nullopt;
        }

    private:
        Listener& mBaseListener;
        std::mutex mMutex;
        std::optional<std::string> mLabelUpdate;
        bool mImportantLabel = false;
        std::optional<std::size_t> mRangeUpdate;
        std::optional<std::size_t> mProgressUpdate;
    };
}
