#pragma once

#include <array>
#include <cstddef>
#include <typeinfo>

namespace Rtx
{
    /// **What a question answers for each dynamic class, learned once a class**, in a table of `Kept` classes scanned
    /// in order: keyed on the class's `std::type_info`, so an object met again costs a pointer compare rather than the
    /// casts its first ask took. The pointer is a cache key and not a claim: a second `type_info` of one class misses
    /// and is learned twice, never wrongly. A class past the table is learned at every ask, which is slower and as
    /// right. One per thread, because a miss writes the table.
    template <class Answer, std::size_t Kept>
    class ClassAnswers
    {
    public:
        /// What `type` answers: the table's, else `learn()`, kept while the table has room.
        template <class Learn>
        Answer of(const std::type_info& type, Learn&& learn)
        {
            for (std::size_t at = 0; at < mHeld; ++at)
                if (mLearned[at].mClass == &type)
                    return mLearned[at].mAnswer;

            const Answer answer = learn();
            if (mHeld < Kept)
            {
                mLearned[mHeld] = Learned{ .mClass = &type, .mAnswer = answer };
                ++mHeld;
            }
            return answer;
        }

    private:
        struct Learned
        {
            const std::type_info* mClass = nullptr;
            Answer mAnswer{};
        };

        std::array<Learned, Kept> mLearned{};
        std::size_t mHeld = 0;
    };
}
