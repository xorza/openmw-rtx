#pragma once

#include <cassert>
#include <concepts>
#include <optional>
#include <utility>
#include <variant>

namespace Misc
{
    /// The failing half of a `Result`, named where it is made — Rust's `Err`. Named rather than
    /// converted, so a `Result` whose two types convert into each other is never ambiguous.
    template <class E>
    struct Err
    {
        E mError;
    };

    /// A value, or why there is none — Rust's `Result`. For a failure the caller is expected to
    /// meet and go on from: content refused item by item while the game goes on, a file that
    /// would not read or write, a value a user typed. A contract the code broke is an assert, and
    /// a failure nothing can go on from is an exception. Not to be dropped: a result nobody reads
    /// is a failure nobody heard of.
    template <class T, class E>
    class [[nodiscard]] Result
    {
    public:
        template <class U>
        requires std::constructible_from<T, U&&> Result(U&& value)
            : mState(std::in_place_index<0>, std::forward<U>(value))
        {
        }

        template <class F>
        requires std::constructible_from<E, F&&> Result(Err<F> error)
            : mState(std::in_place_index<1>, std::move(error.mError))
        {
        }

        bool isOk() const { return mState.index() == 0; }

        const T& value() const
        {
            assert(isOk() && "the value of a result that failed");
            return std::get<0>(mState);
        }

        /// The same, for a value moved out: a resource has one owner, and the result was only its
        /// way here.
        T& value()
        {
            assert(isOk() && "the value of a result that failed");
            return std::get<0>(mState);
        }

        const E& error() const
        {
            assert(!isOk() && "the error of a result that holds a value");
            return std::get<1>(mState);
        }

    private:
        std::variant<T, E> mState;
    };

    /// A check's answer: nothing where it passed, and why where it did not — Rust's `Result<(), E>`.
    /// `return {};` is the pass.
    template <class E>
    class [[nodiscard]] Result<void, E>
    {
    public:
        Result() = default;

        template <class F>
        requires std::constructible_from<E, F&&> Result(Err<F> error)
            : mError(std::move(error.mError))
        {
        }

        bool isOk() const { return !mError.has_value(); }

        const E& error() const
        {
            assert(!isOk() && "the error of a check that passed");
            return *mError;
        }

    private:
        std::optional<E> mError;
    };
}
