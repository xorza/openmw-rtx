#pragma once

#include <format>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <boost/any.hpp>
#include <boost/program_options/errors.hpp>
#include <boost/program_options/value_semantic.hpp>

#include <components/misc/strings/conversion.hpp>

namespace RtxTool
{
    /// Where a number given on the line may lie, either end open or closed.
    template <class T>
    struct NumberRange
    {
        T mLow = std::numeric_limits<T>::lowest();
        T mHigh = std::numeric_limits<T>::max();
        bool mLowOpen = false;
        bool mHighOpen = false;

        bool holds(T value) const
        {
            return (mLowOpen ? value > mLow : value >= mLow) && (mHighOpen ? value < mHigh : value <= mHigh);
        }

        /// The range as the help prints it in place of `arg`: `0..1`, `>0`, `0..<24`, or `number`
        /// where it is every finite number of its type.
        std::string describe() const
        {
            const bool low = mLow != std::numeric_limits<T>::lowest();
            const bool high = mHigh != std::numeric_limits<T>::max();
            if (low && high)
                return std::format("{}{}..{}{}", mLow, mLowOpen ? "<" : "", mHighOpen ? "<" : "", mHigh);
            if (low)
                return std::format("{}{}", mLowOpen ? ">" : ">=", mLow);
            if (high)
                return std::format("{}{}", mHighOpen ? "<" : "<=", mHigh);
            return "number";
        }
    };

    /// Every finite number of `T`.
    template <class T>
    NumberRange<T> anyNumber()
    {
        return NumberRange<T>{};
    }

    /// `low` and more, or more than `low` where `open`.
    template <class T>
    NumberRange<T> atLeast(T low, bool open = false)
    {
        return NumberRange<T>{ .mLow = low, .mLowOpen = open };
    }

    /// From `low` to `high`, `high` left out where `highOpen`.
    template <class T>
    NumberRange<T> between(T low, T high, bool highOpen = false)
    {
        return NumberRange<T>{ .mLow = low, .mHigh = high, .mHighOpen = highOpen };
    }

    /// **The one rule for a number the line gives**: the whole text, read as
    /// `Misc::StringUtils::toNumericWhole` reads every number this tool takes — finite, nothing after
    /// it — and inside the range the option states, which the help prints where Boost prints `arg`.
    /// Boost's own reader takes `nan` and `inf`, and a range stated only in the help held nothing.
    template <class T>
    class NumberValue final : public boost::program_options::typed_value<T>
    {
    public:
        explicit NumberValue(const NumberRange<T>& range)
            : boost::program_options::typed_value<T>(nullptr)
            , mRange(range)
        {
            this->value_name(mRange.describe());
        }

        void xparse(boost::any& value, const std::vector<std::string>& tokens) const override
        {
            namespace bpo = boost::program_options;
            const std::string& text = bpo::validators::get_single_string(tokens);
            const std::optional<T> read = Misc::StringUtils::toNumericWhole<T>(text);
            if (!read.has_value() || !mRange.holds(*read))
            {
                bpo::validation_error error(bpo::validation_error::invalid_option_value);
                error.set_substitute("value", text);
                error.m_error_template
                    = "the argument ('%value%') for option '%canonical_option%' is not a number " + mRange.describe();
                throw error;
            }

            value = *read;
        }

    private:
        NumberRange<T> mRange;
    };

    /// A numeric option's value semantic, for `options_description::add_options`, which takes
    /// ownership.
    template <class T>
    NumberValue<T>* number(const NumberRange<T>& range = NumberRange<T>{})
    {
        return new NumberValue<T>(range);
    }
}
