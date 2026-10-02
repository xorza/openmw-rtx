#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/ripples.hpp>
#include <components/rtx/shaders/ripple.h>

#ifndef OPENMW_PROJECT_SOURCE_DIR
#define OPENMW_PROJECT_SOURCE_DIR "."
#endif

namespace MWRender
{
    namespace
    {
        /// The value `source` gives `const float <name> = <value>;`, or nothing where it gives none.
        std::optional<float> constantIn(std::string_view source, std::string_view name)
        {
            const std::string declared = "const float " + std::string(name) + " = ";
            const std::size_t at = source.find(declared);
            if (at == std::string_view::npos)
                return std::nullopt;

            const std::string_view value = source.substr(at + declared.size());
            float read = 0.0f;
            const std::from_chars_result parsed = std::from_chars(value.data(), value.data() + value.size(), read);
            if (parsed.ec != std::errc() || parsed.ptr == value.data() + value.size() || *parsed.ptr != ';')
                return std::nullopt;

            return read;
        }

        /// **The trace's ripple field is the rasterizer's, number for number.** `ripple.h` is read
        /// by GLSL and can include neither `RipplesSurface` nor the rasterizer's shader, so each
        /// figure it restates is held here to its source: the grid, the texel and the step rate to
        /// the surface's constants, and the springs to `applySprings` as the shader file spells
        /// them. A retune upstream fails this, where it would have left the trace's water a
        /// different simulation with no message.
        TEST(RtxRipplesTest, theTracesFieldIsTheRasterizersOwn)
        {
            EXPECT_EQ(Rtx::Shaders::RIPPLE_GRID, RipplesSurface::sRTTSize);
            EXPECT_EQ(Rtx::Shaders::RIPPLE_TEXEL, RipplesSurface::sWorldScaleFactor);
            EXPECT_EQ(static_cast<double>(Rtx::Shaders::RIPPLE_STEP_RATE), RipplesSurface::sUpdateFrequency);

            std::ifstream file(std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "shaders" / "lib"
                / "water" / "ripples.glsl");
            ASSERT_TRUE(file.is_open());
            const std::string source{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };

            EXPECT_EQ(constantIn(source, "a"), Rtx::Shaders::RIPPLE_STIFFNESS);
            EXPECT_EQ(constantIn(source, "udamp"), Rtx::Shaders::RIPPLE_HEIGHT_DAMPING);
            EXPECT_EQ(constantIn(source, "vdamp"), Rtx::Shaders::RIPPLE_VELOCITY_DAMPING);
            EXPECT_EQ(constantIn(source, "nothing"), std::nullopt) << "a name the shader does not declare";
        }
    }
}
