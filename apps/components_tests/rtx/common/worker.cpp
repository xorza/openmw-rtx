#include <thread>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <components/rtx/common/worker.hpp>

namespace Rtx
{
    namespace
    {
        /// A guard holds the thread that built it, and takes the one that adopts it.
        TEST(RtxOwnedByTest, aGuardHoldsTheThreadThatBuiltItUntilAnotherAdoptsIt)
        {
            OwnedBy owner;
            owner.check();

            std::thread other([&] {
                owner.adopt();
                owner.check();
            });
            other.join();

            Testing::expectAssertDies([&] { owner.check(); }, "a member touched from the wrong thread");

            owner.adopt();
            owner.check();
        }
    }
}
