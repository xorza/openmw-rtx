#pragma once

#include <cstdint>

#include "class.hpp"

namespace MWWorld
{
    template <class Derived, class Base = Class>
    class RegisteredClass : public Base
    {
    public:
        static void registerSelf()
        {
            static Derived instance;
            Base::registerClass(instance);
        }

    private:
        friend Derived;

        explicit RegisteredClass(std::uint32_t type)
            : Base(type)
        {
        }
    };
}
