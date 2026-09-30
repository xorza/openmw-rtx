#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtxvulkan/scene/sceneslots.hpp>

namespace Rtx
{
    namespace
    {
        /// **A picture's slot still out when the slots are taken apart is named there.** A view that
        /// outlives its renderer holds a scene for nothing, and without this nothing says so. Two
        /// slots taken and given back leave nothing out; one taken and kept dies with the message.
        TEST(RtxSceneSlotsTest, slotsTakenApartWithAPicturesSlotOutDie)
        {
            {
                SceneSlots slots;
                const SceneSlot first = slots.add();
                const SceneSlot second = slots.add();
                slots.drop(first);
                slots.drop(second);
            }

            Testing::expectAssertDies(
                [] {
                    SceneSlots slots;
                    slots.add();
                },
                "a renderer taken apart with a picture's scene slot still out");
        }
    }
}
