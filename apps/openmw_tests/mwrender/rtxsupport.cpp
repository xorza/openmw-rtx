#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/rendersupport.hpp>
#include <apps/openmw/mwrender/rtx/rtxsupport.hpp>

namespace MWRender
{
    namespace
    {
        using Key = std::pair<std::string, std::string>;

        std::filesystem::path sourceRoot()
        {
            return std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR };
        }

        std::string contentsOf(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }

        /// The key each `Settings::category().mMember` names, as the category files declare them:
        /// the accessor's struct out of `values.hpp`, the member's category and name out of its
        /// struct's file.
        class SettingNames
        {
        public:
            SettingNames()
            {
                const std::string values = contentsOf(sourceRoot() / "components/settings/values.hpp");
                const std::regex accessor(R"(inline\s+(\w+)&\s+(\w+)\(\))");
                for (std::sregex_iterator it(values.begin(), values.end(), accessor), end; it != end; ++it)
                    mStructOf[(*it)[2]] = (*it)[1];

                const std::regex structName(R"(struct\s+(\w+Category))");
                const std::regex member(R"rx((m\w+)\{\s*mIndex,\s*"([^"]+)",\s*"([^"]+)")rx");
                for (const auto& entry :
                    std::filesystem::directory_iterator(sourceRoot() / "components/settings/categories"))
                {
                    const std::string text = contentsOf(entry.path());
                    std::smatch named;
                    if (!std::regex_search(text, named, structName))
                        continue;
                    for (std::sregex_iterator it(text.begin(), text.end(), member), end; it != end; ++it)
                    {
                        const Key key{ (*it)[2], (*it)[3] };
                        mKeyOf[{ named[1], (*it)[1] }] = key;
                        mKeys.insert(key);
                    }
                }
            }

            /// Every key `Settings::accessor().mMember` reads in `text`, and what it could not name.
            void readIn(const std::string& text, std::set<Key>& keys, std::set<std::string>& unknown) const
            {
                const std::regex read(R"(Settings::(\w+)\(\)\.(m\w+))");
                for (std::sregex_iterator it(text.begin(), text.end(), read), end; it != end; ++it)
                {
                    const auto structIt = mStructOf.find((*it)[1]);
                    const auto keyIt
                        = structIt == mStructOf.end() ? mKeyOf.end() : mKeyOf.find({ structIt->second, (*it)[2] });
                    if (keyIt == mKeyOf.end())
                        unknown.insert((*it)[0]);
                    else
                        keys.insert(keyIt->second);
                }
            }

            bool exists(const Key& key) const { return mKeys.contains(key); }

            bool hasCategory(const std::string& category) const
            {
                for (const Key& key : mKeys)
                    if (key.first == category)
                        return true;
                return false;
            }

        private:
            std::map<std::string, std::string> mStructOf;
            std::map<std::pair<std::string, std::string>, Key> mKeyOf;
            std::set<Key> mKeys;
        };

        /// **Every key the game's renderers read is decided by the ray tracer's declaration**, and
        /// so is every control of the settings window's rendering pages: a key upstream adds to the
        /// rasterizer, or to the window, fails here until the ray tracer's table says whether it
        /// honours it, and why not where it does not. The rasterizer's own files and the shared
        /// ones in `mwrender` are read, the ray tracer's own are not; and the window's controls,
        /// bound by their user strings in the layout or by hand in `settingswindow.cpp`, in the
        /// categories that are rendering's alone.
        ///
        /// **And the table names nothing that is not a setting**, so a key renamed upstream, or
        /// spelt wrong here, is a failure and not an answer to nothing.
        TEST(RtxSupportTest, everyKeyTheRenderersAndTheirWindowReadIsDecidedAndEveryNamedKeyExists)
        {
            const SettingNames names;
            std::set<Key> read;
            std::set<std::string> unknown;

            const std::filesystem::path render = sourceRoot() / "apps/openmw/mwrender";
            for (const auto& entry : std::filesystem::recursive_directory_iterator(render))
            {
                const std::filesystem::path relative = entry.path().lexically_relative(render);
                if (!entry.is_regular_file() || *relative.begin() == "rtx")
                    continue;
                if (entry.path().extension() == ".cpp" || entry.path().extension() == ".hpp")
                    names.readIn(contentsOf(entry.path()), read, unknown);
            }
            EXPECT_TRUE(unknown.empty()) << "a read this test cannot name: " << *unknown.begin();
            ASSERT_GT(read.size(), 100u) << "the render files read next to nothing, so the scan found nothing";

            const std::set<std::string> rendering{ "Camera", "Fog", "Groundcover", "Post Processing", "RTX", "Shaders",
                "Shadows", "Stereo", "Stereo View", "Terrain", "Video", "Water" };
            std::set<Key> window;
            names.readIn(contentsOf(sourceRoot() / "apps/openmw/mwgui/settingswindow.cpp"), window, unknown);
            const std::string layout = contentsOf(sourceRoot() / "files/data/mygui/openmw_settings_window.layout");
            const std::regex bound(
                R"rx(key="SettingCategory" value="([^"]+)"\s*/>\s*<UserString key="SettingName" value="([^"]+)")rx");
            for (std::sregex_iterator it(layout.begin(), layout.end(), bound), end; it != end; ++it)
                window.insert({ (*it)[1], (*it)[2] });
            ASSERT_GT(window.size(), 20u) << "the window binds next to nothing, so the scan found nothing";
            for (const Key& key : window)
                if (rendering.contains(key.first))
                    read.insert(key);

            const RenderSupport& support = rtxSupport();
            for (const Key& key : read)
                EXPECT_TRUE(support.namesSetting(key.first, key.second))
                    << "[" << key.first << "] " << key.second << " is read and the ray tracer has not decided it";

            for (const SettingSupport& entry : support.getSettings())
            {
                const Key key{ std::string(entry.mCategory), std::string(entry.mName) };
                if (entry.mName.empty())
                    EXPECT_TRUE(names.hasCategory(key.first)) << "[" << key.first << "] is no category";
                else
                    EXPECT_TRUE(names.exists(key)) << "[" << key.first << "] " << key.second << " is no setting";
            }
        }

        /// **What the ray tracer cannot draw is declined with a reason**, every request a script or
        /// a key makes of the picture among them: F2's shader chain, and F3's and F4's overlays, which
        /// went silent under it.
        TEST(RtxSupportTest, theOverlaysAndTheShaderChainAreDeclinedWithAReason)
        {
            const RenderSupport& support = rtxSupport();
            for (const PictureRequest request :
                { PictureRequest::ShaderReload, PictureRequest::LiveShaderReload, PictureRequest::StatsOverlay })
                EXPECT_FALSE(support.declinedRequest(request).empty()) << static_cast<int>(request);
            EXPECT_FALSE(support.declinedSetting("Post Processing", "enabled").empty());
        }
    }
}
