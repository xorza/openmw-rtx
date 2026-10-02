#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
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

        /// **The page a player reads lists what the ray tracer declines, and only that, with the
        /// reason the window and the console give**: `rtx.rst`'s list, one line each, read back
        /// against the declaration — a declined key the page leaves out, or one the page keeps after
        /// the renderer took it up, fails here.
        TEST(RtxSupportTest, theSettingsPageListsWhatTheRayTracerDeclines)
        {
            const std::string page = contentsOf(sourceRoot() / "docs/source/reference/modding/settings/rtx.rst");
            std::set<std::tuple<std::string, std::string, std::string>> listed;
            const std::regex keyLine(R"rx(\* ``\[([^\]]+)\] ([^`]+)``: (.+))rx");
            const std::regex categoryLine(R"rx(\* ``\[([^\]]+)\]`` every key: (.+))rx");
            std::istringstream lines(page);
            for (std::string line; std::getline(lines, line);)
            {
                std::smatch match;
                if (std::regex_match(line, match, keyLine))
                    listed.insert({ match[1], match[2], match[3] });
                else if (std::regex_match(line, match, categoryLine))
                    listed.insert({ match[1], "", match[2] });
            }

            const RenderSupport& support = rtxSupport();
            std::set<std::tuple<std::string, std::string, std::string>> declined;
            for (const SettingSupport& entry : support.getSettings())
                if (!entry.mDeclined.empty())
                    declined.insert(
                        { std::string(entry.mCategory), std::string(entry.mName), std::string(entry.mDeclined) });
            EXPECT_EQ(listed, declined) << "rtx.rst's list is not the ray tracer's declaration";

            const auto statedFor = [&](std::string_view command, std::string_view reason) {
                std::istringstream again(page);
                for (std::string line; std::getline(again, line);)
                    if (line.starts_with("* ") && line.find(command) != std::string::npos
                        && line.ends_with(": " + std::string(reason)))
                        return true;
                return false;
            };
            for (const ModeSupport& mode : support.getModes())
            {
                ASSERT_EQ(mode.mMode, Render_Wireframe) << "a declined mode this test has no command for";
                EXPECT_TRUE(statedFor("``tww``", mode.mDeclined)) << "the wireframe";
            }
            for (const RequestSupport& request : support.getRequests())
            {
                const std::string_view command = request.mRequest == ScriptRequest::Borders ? "``ToggleBorders``"
                    : request.mRequest == ScriptRequest::ShaderReload ? "``debug.triggerShaderReload``"
                                                                      : "``debug.setShaderHotReloadEnabled``";
                EXPECT_TRUE(statedFor(command, request.mDeclined)) << command;
            }
        }
    }
}
