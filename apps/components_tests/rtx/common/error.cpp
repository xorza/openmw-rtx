#include <string>

#include <gtest/gtest.h>

#include <components/crashcatcher/crashnote.hpp>
#include <components/rtx/common/error.hpp>

namespace
{
    /// **An error says what its thread was doing as it was raised**, which the note forgets as the
    /// stack unwinds: each of the three that reach the player, the note's path whole, nothing added
    /// where the thread noted nothing, and nothing twice where an error is raised again as another.
    TEST(RtxErrorTest, anErrorCarriesWhatItsThreadWasDoing)
    {
        EXPECT_EQ(std::string(Rtx::DeviceError("vkQueueSubmit failed").what()), "vkQueueSubmit failed");

        const Crash::NoteScope frame("drawing frame 12");
        const Crash::NoteScope texture("staging the texture \"{}\"", "textures/tx_a.dds");
        const std::string noted = ", while drawing frame 12 > staging the texture \"textures/tx_a.dds\"";
        EXPECT_EQ(std::string(Rtx::DeviceError("no room").what()), "no room" + noted);
        EXPECT_EQ(std::string(Rtx::InputError("a bad shader").what()), "a bad shader" + noted);
        EXPECT_EQ(std::string(Rtx::UpscalerRefused("NGX refused").what()), "NGX refused" + noted);

        const Rtx::UpscalerRefused refused("NGX refused");
        EXPECT_EQ(std::string(Rtx::DeviceError(refused.what()).what()), "NGX refused" + noted);
    }
}
