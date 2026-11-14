#pragma once

#include "aipackage.hpp"

namespace MWMechanics
{
    template <class T>
    struct TypedAiPackage : public AiPackage
    {
        std::unique_ptr<AiPackage> clone() const override { return std::make_unique<T>(*static_cast<const T*>(this)); }

    private:
        friend T;

        TypedAiPackage()
            : AiPackage(T::getTypeId(), T::makeDefaultOptions())
        {
        }

        explicit TypedAiPackage(bool repeat)
            : AiPackage(T::getTypeId(), T::makeDefaultOptions().withRepeat(repeat))
        {
        }

        explicit TypedAiPackage(const Options& options)
            : AiPackage(T::getTypeId(), options)
        {
        }

        template <class Derived>
        explicit TypedAiPackage(Derived*)
            : AiPackage(Derived::getTypeId(), Derived::makeDefaultOptions())
        {
        }
    };
}
