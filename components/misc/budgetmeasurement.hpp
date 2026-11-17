#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Misc
{

    class BudgetMeasurement
    {
        std::array<float, 4> mBudgetHistory{};
        std::array<std::uint32_t, 4> mBudgetStepCount{};

    public:
        explicit BudgetMeasurement(const float defaultExpense)
        {
            mBudgetHistory = { defaultExpense, defaultExpense, defaultExpense, defaultExpense };
            mBudgetStepCount = { 1, 1, 1, 1 };
        }

        void reset(const float defaultExpense)
        {
            mBudgetHistory = { defaultExpense, defaultExpense, defaultExpense, defaultExpense };
            mBudgetStepCount = { 1, 1, 1, 1 };
        }

        void update(double delta, std::uint32_t stepCount, std::size_t cursor)
        {
            mBudgetHistory[cursor % 4] = static_cast<float>(delta);
            mBudgetStepCount[cursor % 4] = stepCount;
        }

        float get() const
        {
            float sum = (mBudgetHistory[0] + mBudgetHistory[1] + mBudgetHistory[2] + mBudgetHistory[3]);
            std::uint32_t stepCountSum
                = (mBudgetStepCount[0] + mBudgetStepCount[1] + mBudgetStepCount[2] + mBudgetStepCount[3]);
            return sum / static_cast<float>(stepCountSum);
        }
    };

}
