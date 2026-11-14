#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <components/files/conversion.hpp>
#include <components/rtxvulkan/shaders/shared/counts.h>

namespace Rtx
{
    namespace
    {
        /// `path` with forward slashes and as UTF-8, as a tree path is written in a list.
        std::string genericName(const std::filesystem::path& path)
        {
            const std::u8string spelled = path.generic_u8string();
            return std::string(spelled.begin(), spelled.end());
        }

        /// What the tree says about itself, asked of the sources rather than of a build, so a rule
        /// holds on every run and not only when somebody remembers to check it.
        const std::filesystem::path sRoot{ OPENMW_PROJECT_SOURCE_DIR };
        const std::filesystem::path sCore = sRoot / "components" / "rtx";
        const std::filesystem::path sBackend = sRoot / "components" / "rtxvulkan";
        const std::filesystem::path sTests = sRoot / "apps" / "components_tests";

        /// The fork's own code, which the conventions the rules hold are written for: upstream's is not
        /// brought in line with them.
        const std::vector<std::filesystem::path> sForkCode{ sCore, sBackend, sRoot / "components" / "myguirtx",
            sRoot / "components" / "contentstore", sRoot / "apps" / "openmw" / "mwrender" / "rtx",
            sRoot / "apps" / "rtxtool", sRoot / "apps" / "rtxbuild" };

        bool isWordChar(const char c)
        {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        std::size_t skipSpace(const std::string_view text, std::size_t at)
        {
            while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])))
                ++at;
            return at;
        }

        std::string_view codeOf(const std::string_view line)
        {
            return line.substr(0, line.find("//"));
        }

        /// What an `#include` line names, and whether between quotes or angle brackets.
        struct Included
        {
            std::string mPath;
            bool mQuoted = false;
        };

        /// What an `#include` line names, or nothing for any other line.
        std::optional<Included> includedBy(const std::string_view line)
        {
            if (!line.starts_with("#include"))
                return std::nullopt;

            const std::size_t open = line.find_first_of("\"<");
            if (open == std::string_view::npos)
                return std::nullopt;

            const bool quoted = line[open] == '"';
            const std::size_t close = line.find(quoted ? '"' : '>', open + 1);
            if (close == std::string_view::npos)
                return std::nullopt;

            return Included{ .mPath = std::string(line.substr(open + 1, close - open - 1)), .mQuoted = quoted };
        }

        std::string joined(const std::vector<std::string>& lines)
        {
            std::string all;
            for (const std::string& line : lines)
                all += line + '\n';
            return all;
        }

        /// Where a quoted `#include` is looked for when the including file's folder does not hold
        /// it: the two roots the shader build passes with `-I`.
        const std::array<std::filesystem::path, 2> sShaderRoots{ sBackend / "shaders", sCore / "shaders" };

        /// **The tree as the rules read it, each file read once for the suite** and each file's own includes found
        /// once: the rules walk the same folders, and a walk a rule read the tree with cost some ten thousand opens
        /// a run.
        class SourceTree
        {
        public:
            /// Every line of `file`, read the first time it is asked for.
            const std::vector<std::string>& linesOf(const std::filesystem::path& file)
            {
                const std::filesystem::path normal = file.lexically_normal();
                const auto found = mLines.find(normal);
                if (found != mLines.end())
                    return found->second;

                std::vector<std::string>& lines = mLines[normal];
                std::ifstream in(normal);
                for (std::string line; std::getline(in, line);)
                    lines.push_back(line);
                return lines;
            }

            /// Every regular file anywhere under `place`, in path order, listed the first time it is asked for.
            const std::vector<std::filesystem::path>& filesUnder(const std::filesystem::path& place)
            {
                const auto found = mListings.find(place);
                if (found != mListings.end())
                    return found->second;

                std::vector<std::filesystem::path>& files = mListings[place];
                for (const std::filesystem::directory_entry& entry :
                    std::filesystem::recursive_directory_iterator(place))
                    if (entry.is_regular_file())
                        files.push_back(entry.path().lexically_normal());
                std::ranges::sort(files);
                return files;
            }

            /// Every file `file` reaches through `#include`, itself included, by the paths the sources spell: a
            /// quoted one beside the including file or under a shader root, and one in angle brackets from the
            /// source root where it names `apps/`. Not `components/`, which reaches upstream's whole graph and
            /// nothing a rule here asks about.
            std::set<std::filesystem::path> reachedBy(const std::filesystem::path& file)
            {
                std::set<std::filesystem::path> reached;
                std::vector<std::filesystem::path> pending{ file.lexically_normal() };
                while (!pending.empty())
                {
                    const std::filesystem::path next = std::move(pending.back());
                    pending.pop_back();
                    if (!reached.insert(next).second)
                        continue;
                    const std::vector<std::filesystem::path>& named = includesOf(next);
                    pending.insert(pending.end(), named.begin(), named.end());
                }
                return reached;
            }

        private:
            std::map<std::filesystem::path, std::vector<std::string>> mLines;
            std::map<std::filesystem::path, std::vector<std::filesystem::path>> mListings;
            std::map<std::filesystem::path, std::vector<std::filesystem::path>> mIncludes;

            /// The files `file` includes itself, by `reachedBy`'s rules.
            const std::vector<std::filesystem::path>& includesOf(const std::filesystem::path& file)
            {
                const auto found = mIncludes.find(file);
                if (found != mIncludes.end())
                    return found->second;

                std::vector<std::filesystem::path> named;
                for (const std::string& line : linesOf(file))
                {
                    const std::optional<Included> included = includedBy(line);
                    if (!included.has_value())
                        continue;

                    if (included->mQuoted)
                    {
                        if (std::filesystem::path beside = file.parent_path() / included->mPath;
                            std::filesystem::exists(beside))
                        {
                            named.push_back(beside.lexically_normal());
                            continue;
                        }
                        for (const std::filesystem::path& root : sShaderRoots)
                            if (std::filesystem::path under = root / included->mPath; std::filesystem::exists(under))
                            {
                                named.push_back(under.lexically_normal());
                                break;
                            }
                    }
                    else if (included->mPath.starts_with("apps/"))
                    {
                        if (const std::filesystem::path under = sRoot / included->mPath; std::filesystem::exists(under))
                            named.push_back(under.lexically_normal());
                    }
                }
                return mIncludes.emplace(file, std::move(named)).first->second;
            }
        };

        /// The `vkDestroyX(` or `mDestroyX(` call on `code`, as the name called, or nothing.
        ///
        /// By hand and not by `std::regex`, because GCC 16 under AddressSanitizer reads
        /// `<regex>`'s own `std::function` as maybe-uninitialized and `-Werror` makes that a
        /// build that does not exist.
        std::optional<std::string> destroyCallOn(const std::string_view code)
        {
            for (std::size_t at = code.find("Destroy"); at != std::string_view::npos; at = code.find("Destroy", at + 1))
            {
                std::size_t start = 0;
                if (at >= 2 && code.substr(at - 2, 2) == "vk")
                    start = at - 2;
                else if (at >= 1 && code[at - 1] == 'm')
                    start = at - 1;
                else
                    continue;

                if (start > 0 && isWordChar(code[start - 1]))
                    continue;

                std::size_t end = at + std::string_view("Destroy").size();
                while (end < code.size() && std::isalpha(static_cast<unsigned char>(code[end])))
                    ++end;

                const std::size_t paren = skipSpace(code, end);
                if (paren < code.size() && code[paren] == '(')
                    return std::string(code.substr(start, end - start));
            }

            return std::nullopt;
        }

        /// `name` on `code` as the thing matched, or nothing.
        std::optional<std::string> spelled(const std::string_view code, const std::string_view name)
        {
            return code.find(name) != std::string_view::npos ? std::optional<std::string>(name) : std::nullopt;
        }

        /// A preprocessor conditional on `code`, `#if`, `#ifdef`, `#ifndef` or `#elif`, as its directive.
        std::optional<std::string> conditionalOn(const std::string_view code)
        {
            const std::size_t hash = skipSpace(code, 0);
            if (hash >= code.size() || code[hash] != '#')
                return std::nullopt;
            const std::size_t word = skipSpace(code, hash + 1);
            std::size_t end = word;
            while (end < code.size() && isWordChar(code[end]))
                ++end;
            const std::string_view directive = code.substr(word, end - word);
            if (directive == "if" || directive == "ifdef" || directive == "ifndef" || directive == "elif")
                return "#" + std::string(directive);
            return std::nullopt;
        }

        /// What names a GPU's vendor or driver on `code`: the vendor's number Vulkan reports, a driver's
        /// identity, or the two vendors' PCI numbers.
        std::optional<std::string> vendorOn(const std::string_view code)
        {
            for (const std::string_view name : { "vendorID", "VK_DRIVER_ID_", "0x10de", "0x10DE", "0x1002" })
                if (code.find(name) != std::string_view::npos)
                    return std::string(name);
            return std::nullopt;
        }

        /// **A rule a line of the tree may break**, one row of `sLineRules`: where it is looked for, what breaks
        /// it, and where it stands broken on purpose. **Every allowance is met**, or it is a dead entry that
        /// would let a later break through unseen, and **the match is shown to match** a line written for it,
        /// so a rule that finds nothing has shown it could have.
        struct LineRule
        {
            std::string_view mName;
            std::vector<std::filesystem::path> mPlaces;

            /// The thing broken on `code`, comments stripped, or nothing.
            std::optional<std::string> (*mMatch)(std::string_view code);

            /// A line `mMatch` must find a break on.
            std::string_view mExample;

            /// Files the rule does not hold in, by name, each of which breaks it, for a reason given where the
            /// row stands.
            std::set<std::string, std::less<>> mAllowedFiles{};

            /// Things matched that the rule allows anywhere, each of which is met.
            std::set<std::string, std::less<>> mAllowedNames{};

            /// What a break says to do.
            std::string_view mFix;

            /// The extensions the rule reads.
            std::set<std::string, std::less<>> mExtensions{ ".cpp", ".hpp" };
        };

        void PrintTo(const LineRule& rule, std::ostream* out)
        {
            *out << rule.mName;
        }

        const std::array sLineRules{
            /// **Every Vulkan handle the backend owns is held by `Rtx::Owned`.** `owned.hpp` is "the one place
            /// `vkDestroyX(device, handle, allocator)` is spelled"; a class that spells it itself pays a destructor,
            /// a null check and a `const Device&` member. A loaded destroyer (`mDestroyX`) is matched too. The
            /// surface takes the instance, which `Owned` has no parent of, and ends through a loaded entry point.
            LineRule{ .mName = "everyDeviceParentedVulkanHandleIsHeldByOwned",
                .mPlaces = { sBackend },
                .mMatch = destroyCallOn,
                .mExample = "vkDestroyBuffer(device, buffer, nullptr);",
                .mAllowedFiles = { "owned.hpp" },
                .mAllowedNames = { "mDestroySurface" },
                .mFix = "hold it as Owned<Handle, vkDestroyX> and delete the destructor" },
            /// **A handle ended at once is held by `Rtx::Immediate` only where the graveyard cannot outlive it.**
            /// Everything else is `Owned`, which buries, so a handle a submit may still read is never ended under
            /// it. The three outside it say why where they are declared: the clock's semaphore and the pipeline
            /// cache, which the device takes apart after its graveyard, and the swapchain.
            LineRule{ .mName = "onlyWhatOutlivesTheGraveyardIsEndedAtOnce",
                .mPlaces = { sBackend },
                .mMatch = [](std::string_view code) { return spelled(code, "Immediate<"); },
                .mExample = "Immediate<VkFence, vkDestroyFence> mFence;",
                .mAllowedFiles = { "timeline.hpp", "timeline.cpp", "pipelinecache.hpp", "pipelinecache.cpp",
                    "swapchain.hpp", "swapchain.cpp" },
                .mFix = "hold it as Owned, which buries it" },
            /// **A call on the device says a lost device as one**: `checkVk` takes the device wherever one stands,
            /// so `VK_ERROR_DEVICE_LOST` from any call ends as a crash with the fault, and the form without one is
            /// for the instance, a physical device, the surface, and the device's own making.
            LineRule{ .mName = "onlyTheInstanceAndPhysicalDeviceCheckWithoutADevice",
                .mPlaces = { sBackend },
                .mMatch = [](std::string_view code) { return spelled(code, "checkVkWithoutDevice("); },
                .mExample = "checkVkWithoutDevice(vkQueueWaitIdle(queue), \"wait\");",
                .mAllowedFiles = { "result.hpp", "result.cpp", "owned.hpp", "instance.cpp", "physicaldevice.cpp",
                    "device.cpp", "surface.cpp", "zonecounters.cpp" },
                .mFix = "hand `checkVk` the device, whose loss it would otherwise throw rather than report" },
            /// **A scene slot a renderer hands out is held by `Rtx::ViewScene`**, which gives it back. A take in a
            /// constructor and a drop in a destructor are two classes and a throw apart: a constructor that unwinds
            /// after the take leaks the slot for the renderer's life, and a drop of a slot already dropped puts it
            /// on the free list twice. The renderers that hand slots out, and the handle, spell the pair.
            LineRule{ .mName = "everyViewSceneIsHeldByViewScene",
                .mPlaces
                = { sCore, sBackend, sRoot / "apps" / "openmw" / "mwrender" / "rtx", sRoot / "apps" / "rtxtool" },
                .mMatch =
                    [](std::string_view code) {
                        const std::optional<std::string> take = spelled(code, "addViewScene(");
                        return take.has_value() ? take : spelled(code, "dropViewScene(");
                    },
                .mExample = "const Index scene = renderer.addViewScene();",
                .mAllowedFiles = { "viewscene.cpp", "sceneholder.hpp", "vulkanrenderer.hpp", "vulkanrenderer.cpp",
                    "nullrenderer.hpp", "nullrenderer.cpp" },
                .mFix = "hold an Rtx::ViewScene and delete the drop" },
            /// **A death the fork's tests assert goes through `Testing::expectDies`**, whose child keeps no core: a
            /// bare one writes a core to the journal on every run. This file names the macros it looks for.
            LineRule{ .mName = "everyDeathIsAssertedWithoutACore",
                .mPlaces = { sTests / "rtx", sTests / "rtxvulkan", sTests / "myguirtx", sTests / "rtxtool",
                    sRoot / "apps" / "openmw_tests" / "mwrender" },
                .mMatch =
                    [](std::string_view code) {
                        const std::optional<std::string> death = spelled(code, "_DEATH(");
                        return death.has_value() ? death : spelled(code, "_EXIT(");
                    },
                .mExample = "EXPECT_DEATH(run(), \"\");",
                .mAllowedFiles = { "death.hpp", "sourcetree.cpp" },
                .mFix = "assert it with Testing::expectDies, which keeps its core out of the journal" },
            /// **The preprocessor switches only where nothing else can** (`AGENTS.md`, Conventions): a build flag is
            /// read once into a `constexpr bool`, and code only one build or system has is a file CMake chooses.
            /// `asserts.hpp` reads the standard's `NDEBUG`, which is defined or not rather than nought or one.
            LineRule{ .mName = "noFileOfTheForkSwitchesOnThePreprocessor",
                .mPlaces = sForkCode,
                .mMatch = conditionalOn,
                .mExample = "#ifdef _WIN32",
                .mAllowedFiles = { "asserts.hpp" },
                .mFix = "read the flag once into a constexpr bool, or let CMake choose a file" },
            /// **GPU vendors stand behind one entity** (`AGENTS.md`, Conventions): the driver's identity is read
            /// once, at device creation (`physicaldevice.cpp`), into the profile that answers for it
            /// (`driverprofile.cpp`). The pipeline cache's header names the device it was made on, which Vulkan's
            /// own header spells by vendor, and asks nothing of the vendor.
            LineRule{ .mName = "noFileOfTheForkAsksTheVendor",
                .mPlaces = sForkCode,
                .mMatch = vendorOn,
                .mExample = "if (properties.vendorID == 0x10de)",
                .mAllowedFiles = { "driverprofile.cpp", "physicaldevice.cpp", "pipelinecache.cpp" },
                .mFix = "ask a capability of the driver's profile, chosen once at device creation" },
        };

        struct RtxSourceTreeTest : ::testing::Test
        {
            static void SetUpTestSuite() { sTree = std::make_unique<SourceTree>(); }
            static void TearDownTestSuite() { sTree.reset(); }

            static inline std::unique_ptr<SourceTree> sTree;
        };

        struct RtxLineRuleTest : RtxSourceTreeTest, ::testing::WithParamInterface<LineRule>
        {
        };

        TEST_P(RtxLineRuleTest, holdsAndEveryAllowanceIsMet)
        {
            const LineRule& rule = GetParam();
            ASSERT_TRUE(rule.mMatch(rule.mExample).has_value()) << "the rule finds nothing in its own example";

            std::vector<std::string> found;
            std::set<std::string, std::less<>> metFiles;
            std::set<std::string, std::less<>> metNames;
            for (const std::filesystem::path& place : rule.mPlaces)
                for (const std::filesystem::path& file : sTree->filesUnder(place))
                {
                    if (!rule.mExtensions.contains(genericName(file.extension())))
                        continue;
                    const std::string name = Files::pathToUnicodeString(file.filename());
                    const std::vector<std::string>& lines = sTree->linesOf(file);
                    for (std::size_t at = 0; at < lines.size(); ++at)
                    {
                        const std::optional<std::string> broken = rule.mMatch(codeOf(lines[at]));
                        if (!broken.has_value())
                            continue;
                        if (rule.mAllowedFiles.contains(name))
                            metFiles.insert(name);
                        else if (rule.mAllowedNames.contains(*broken))
                            metNames.insert(*broken);
                        else
                            found.push_back(genericName(file.lexically_relative(sRoot)) + ':' + std::to_string(at + 1)
                                + ": " + std::string(codeOf(lines[at])));
                    }
                }

            EXPECT_TRUE(found.empty()) << rule.mFix << ", or allow the file in `sLineRules` with the reason:\n"
                                       << joined(found);
            for (const std::string& allowed : rule.mAllowedFiles)
                EXPECT_TRUE(metFiles.contains(allowed)) << allowed << " is allowed and breaks nothing: drop it";
            for (const std::string& allowed : rule.mAllowedNames)
                EXPECT_TRUE(metNames.contains(allowed)) << allowed << " is allowed and met nowhere: drop it";
        }

        INSTANTIATE_TEST_SUITE_P(RtxSourceTree, RtxLineRuleTest, ::testing::ValuesIn(sLineRules),
            [](const ::testing::TestParamInfo<LineRule>& info) { return std::string(info.param.mName); });

        /// **Every header of the fork's includes itself once**, by `#pragma once` (`AGENTS.md`, Conventions). The
        /// headers GLSL reads as well are `.h`, which `portable.h` says why are guarded otherwise.
        TEST_F(RtxSourceTreeTest, everyHeaderOfTheForkIsPragmaOnce)
        {
            std::vector<std::string> found;
            std::size_t headers = 0;
            for (const std::filesystem::path& place : sForkCode)
                for (const std::filesystem::path& file : sTree->filesUnder(place))
                {
                    if (file.extension() != ".hpp")
                        continue;
                    ++headers;
                    const std::vector<std::string>& lines = sTree->linesOf(file);
                    if (std::ranges::find(lines, "#pragma once") == lines.end())
                        found.push_back(genericName(file.lexically_relative(sRoot)));
                }

            EXPECT_GT(headers, 0u) << "no header read";
            EXPECT_TRUE(found.empty()) << "a header with no `#pragma once`:\n" << joined(found);
        }

        /// No program links the Vulkan loader: volk loads it where an instance is made
        /// (`openmw-rtx-vulkan-api`).
        ///
        /// **A program that imports the loader does not start without it**, nor says why it cannot
        /// draw, and on Windows one that imports a function an old loader lacks stops in a
        /// dialog before `main`. `VK_NO_PROTOTYPES` keeps every call that bypasses volk from
        /// compiling, so a link of the import library is the one way back, and this finds it.
        TEST_F(RtxSourceTreeTest, noProgramLinksTheVulkanLoader)
        {
            std::vector<std::string> found;
            bool namesTheApi = false;
            const auto scan = [&](const std::filesystem::path& file) {
                const std::vector<std::string>& lines = sTree->linesOf(file);
                for (std::size_t at = 0; at < lines.size(); ++at)
                {
                    const std::string_view code = std::string_view(lines[at]).substr(0, lines[at].find('#'));
                    if (code.find("Vulkan::Vulkan") != std::string_view::npos)
                        found.push_back(genericName(file.lexically_relative(sRoot)) + ':' + std::to_string(at + 1)
                            + ": " + lines[at]);
                    namesTheApi = namesTheApi || code.find("openmw-rtx-vulkan-api") != std::string_view::npos;
                }
            };

            scan(sRoot / "CMakeLists.txt");
            for (const char* const place : { "apps", "cmake", "components", "extern" })
                for (const std::filesystem::path& file : sTree->filesUnder(sRoot / place))
                    if (file.filename() == "CMakeLists.txt" || file.extension() == ".cmake")
                        scan(file);

            EXPECT_TRUE(namesTheApi) << "no CMake file read names the target a program links instead";
            EXPECT_TRUE(found.empty()) << "a target links the Vulkan loader; link `openmw-rtx-vulkan-api`:\n"
                                       << joined(found);
        }

        /// No compute shader traces a ray but the trace's stages.
        ///
        /// **A ray query inside a compute dispatch answered differently from run to run** — a candidate
        /// counted twice or not at all — while another process shared the card, and never inside a
        /// ray-tracing launch. So this asks of every `.comp` and what it includes whether it reaches
        /// `rayQueryEXT`. **The trace's stages are dispatches all the same** (`docs/architecture.md`,
        /// "The shaders" and "Every stage is a compute dispatch"): what that costs in repeatability is
        /// measured with `./omw repeat` and not ruled out here. Each of them traces.
        TEST_F(RtxSourceTreeTest, noComputeShaderTracesARay)
        {
            const std::filesystem::path shaders = sBackend / "shaders";
            const std::set<std::string, std::less<>> stages{ "visibility.comp", "direct.comp", "media.comp",
                "panes.comp", "bounce.comp", "landing.comp", "unbounced.comp" };

            std::vector<std::string> found;
            std::set<std::string, std::less<>> traced;
            for (const std::filesystem::path& file : sTree->filesUnder(shaders))
            {
                if (file.extension() != ".comp")
                    continue;

                for (const std::filesystem::path& reached : sTree->reachedBy(file))
                {
                    const std::vector<std::string>& lines = sTree->linesOf(reached);
                    const bool queries = std::ranges::any_of(
                        lines, [](const std::string& line) { return line.find("rayQueryEXT") != std::string::npos; });
                    if (!queries)
                        continue;
                    const std::string name = Files::pathToUnicodeString(file.filename());
                    if (stages.contains(name))
                        traced.insert(name);
                    else
                        found.push_back(name + " reaches " + genericName(reached.lexically_relative(shaders)));
                }
            }

            EXPECT_TRUE(found.empty()) << "a compute shader traces a ray, which answers differently under another "
                                          "process's preemption — make the pass a launch:\n"
                                       << joined(found);
            for (const std::string& stage : stages)
                EXPECT_TRUE(traced.contains(stage)) << stage << " is a trace's stage and traces no ray: drop it";
        }

        /// **Every store of an image goes through the census** (`census.glsl`): `RTX_STORE_COUNTED` for
        /// a float, which counts it, and `RTX_STORE_WORDS` for words, whose packing counted the
        /// floats it took. A raw `imageStore` is a store a NaN crosses uncounted. The probes are the
        /// tests' instruments, which store what a test hands them and run in no frame.
        TEST_F(RtxSourceTreeTest, everyImageStoreGoesThroughTheCensus)
        {
            const std::filesystem::path shaders = sBackend / "shaders";
            const std::filesystem::path census = (shaders / "lib" / "census.glsl").lexically_normal();
            const std::filesystem::path probes = (shaders / "probes").lexically_normal();

            std::vector<std::string> found;
            bool censusStores = false;
            for (const std::filesystem::path& file : sTree->filesUnder(shaders))
            {
                const std::vector<std::string>& lines = sTree->linesOf(file);
                for (std::size_t at = 0; at < lines.size(); ++at)
                {
                    if (lines[at].find("imageStore(") == std::string::npos)
                        continue;
                    if (file == census)
                        censusStores = true;
                    else if (file.parent_path() != probes)
                        found.push_back(genericName(file.lexically_relative(shaders)) + ":" + std::to_string(at + 1));
                }
            }

            EXPECT_TRUE(censusStores) << "the census stores nothing itself, so this matches nothing";
            EXPECT_TRUE(found.empty()) << "a store no census counts — use RTX_STORE_COUNTED or RTX_STORE_WORDS:\n"
                                       << joined(found);
        }

        /// **Every temporal filter weighs its taps by the one gather** (`RTX_HISTORY_SHARES` in
        /// `surfacematch.glsl`): a kernel that calls `historyShare` itself has written the gather
        /// again, and the five copies the reviews found had drifted apart — one lost its "no history"
        /// test. A kernel still reads its own payload at each tap (`historyTap`).
        TEST_F(RtxSourceTreeTest, everyHistoryIsWeighedByTheOneGather)
        {
            const std::filesystem::path shaders = sBackend / "shaders";
            const std::filesystem::path library = (shaders / "lib" / "surfacematch.glsl").lexically_normal();

            std::vector<std::string> found;
            bool gathered = false;
            for (const std::filesystem::path& file : sTree->filesUnder(shaders))
            {
                const std::vector<std::string>& lines = sTree->linesOf(file);
                for (std::size_t at = 0; at < lines.size(); ++at)
                {
                    if (lines[at].find("historyShare(") == std::string::npos)
                        continue;
                    if (file == library)
                        gathered = true;
                    else
                        found.push_back(genericName(file.lexically_relative(shaders)) + ":" + std::to_string(at + 1));
                }
            }

            EXPECT_TRUE(gathered) << "the gather calls no `historyShare`, so this matches nothing";
            EXPECT_TRUE(found.empty()) << "a history weighed outside RTX_HISTORY_SHARES:\n" << joined(found);
        }

        /// **The census has a word for every module the build compiles**, the probes among them,
        /// since the tests' device counts: one past `CENSUS_KERNELS` stops the device as its first
        /// pipeline is made, which a run that never makes it would not see.
        TEST_F(RtxSourceTreeTest, theCensusHasAWordForEveryModule)
        {
            constexpr std::array<std::string_view, 7> stages{ ".comp", ".rgen", ".rchit", ".rahit", ".rmiss", ".vert",
                ".frag" };

            std::uint32_t modules = 0;
            for (const std::filesystem::path& file : sTree->filesUnder(sBackend / "shaders"))
                if (std::ranges::find(stages, genericName(file.extension())) != stages.end())
                    ++modules;

            EXPECT_GT(modules, 0u);
            EXPECT_LE(modules, Shaders::CENSUS_KERNELS);
        }

        /// One library's or program's folders in the order they may include each other: a folder
        /// includes only the folders before it, and a folder inside one of them is a part of it,
        /// which the two may both reach into. `""` is a file straight under the root, which the
        /// tree spells from the source root.
        struct FolderOrder
        {
            std::string_view mRoot;
            std::vector<std::string_view> mOrder;
        };

        const std::array<FolderOrder, 7> sFolderOrders{
            FolderOrder{ "components/rtx",
                { "shaders", "common", "image", "scene", "preprocess", "frame", "world", "renderer", "mirror",
                    "prepare", "environment", "view", "tables" } },
            FolderOrder{ "components/rtxvulkan",
                { "shaders", "spirv", "device", "pipeline", "frame", "texture", "scene", "denoise", "trace", "upscale",
                    "present", "display", "gui", "" } },
            FolderOrder{ "components/myguirtx", { "" } },
            FolderOrder{ "components/contentstore", { "" } },
            FolderOrder{ "apps/openmw/mwrender/rtx", { "" } },
            FolderOrder{ "apps/rtxtool", { "instruments", "model", "" } },
            FolderOrder{ "apps/rtxbuild", { "" } },
        };

        /// **The backend reaches into the core up to the renderer and no further**: what the core makes after
        /// it — the walk of the game's graph, the content's preparation, the sky read from the content, the
        /// pictures away from the eye — is the game side's, and a backend is handed what they made.
        constexpr std::string_view sBackendReachesUpTo = "renderer";

        /// The folder under an order's root that a path stands in: the first of its names, or
        /// `""` for a file straight under the root.
        std::string topFolderOf(const std::filesystem::path& relative)
        {
            const auto first = relative.begin();
            return std::next(first) == relative.end() ? std::string() : first->string();
        }

        const FolderOrder& orderOf(const std::string_view root)
        {
            const auto found = std::ranges::find(sFolderOrders, root, &FolderOrder::mRoot);
            EXPECT_NE(found, sFolderOrders.end()) << root;
            return *found;
        }

        std::optional<std::size_t> rankIn(const FolderOrder& order, const std::string_view folder)
        {
            const auto at = std::ranges::find(order.mOrder, folder);
            if (at == order.mOrder.end())
                return std::nullopt;
            return static_cast<std::size_t>(at - order.mOrder.begin());
        }

        /// **A folder includes only the folders before it**, which is what the folders are: a layer
        /// each, so that what a file may reach is read off where it stands. A quoted include names a
        /// header of the file's own folder, and any other is spelled from the root, so the direction
        /// is read off every line that crosses a folder. Every folder an order names stands in its
        /// root, and the backend reaches into the core no further than `sBackendReachesUpTo`.
        TEST_F(RtxSourceTreeTest, everyFolderIncludesOnlyTheFoldersBeforeIt)
        {
            const FolderOrder& core = orderOf("components/rtx");
            const std::optional<std::size_t> coreReach = rankIn(core, sBackendReachesUpTo);
            ASSERT_TRUE(coreReach.has_value());

            std::vector<std::string> found;
            for (const FolderOrder& order : sFolderOrders)
            {
                const std::filesystem::path root = sRoot / order.mRoot;
                const std::string rooted = std::string(order.mRoot) + '/';

                std::set<std::string, std::less<>> met;
                for (const std::filesystem::path& file : sTree->filesUnder(root))
                {
                    const std::string folder = topFolderOf(file.lexically_relative(root));
                    met.insert(folder);
                    if (file.extension() != ".cpp" && file.extension() != ".hpp")
                        continue;

                    const std::string relative = genericName(file.lexically_relative(sRoot));
                    const std::optional<std::size_t> from = rankIn(order, folder);
                    if (!from.has_value())
                    {
                        found.push_back(relative + ": its folder is in no order — name it in `sFolderOrders`");
                        continue;
                    }

                    for (const std::string& line : sTree->linesOf(file))
                    {
                        const std::optional<Included> included = includedBy(line);
                        if (!included.has_value())
                            continue;

                        if (included->mQuoted)
                        {
                            if (included->mPath.find('/') != std::string::npos
                                || !std::filesystem::exists(file.parent_path() / included->mPath))
                                found.push_back(relative + ": \"" + included->mPath
                                    + "\" is not in its own folder — spell it from the root");
                            continue;
                        }

                        if (order.mRoot.starts_with("components/") && included->mPath.starts_with("apps/"))
                        {
                            found.push_back(relative + ": <" + included->mPath + "> is the game's or a program's");
                            continue;
                        }

                        if (order.mRoot == "components/rtxvulkan" && included->mPath.starts_with("components/rtx/"))
                        {
                            const std::string target = topFolderOf(included->mPath.substr(core.mRoot.size() + 1));
                            const std::optional<std::size_t> to = rankIn(core, target);
                            if (!to.has_value() || *to > *coreReach)
                                found.push_back(relative + ": <" + included->mPath + "> is past the core's `"
                                    + std::string(sBackendReachesUpTo) + "/`, which the backend reaches no further");
                            continue;
                        }

                        if (!included->mPath.starts_with(rooted))
                            continue;

                        if (std::filesystem::path(included->mPath).parent_path()
                            == file.parent_path().lexically_relative(sRoot))
                            found.push_back(relative + ": <" + included->mPath
                                + "> is in its own folder — quote it among the folder's own");

                        const std::string target = topFolderOf(included->mPath.substr(rooted.size()));
                        const std::optional<std::size_t> to = rankIn(order, target);
                        if (!to.has_value())
                            found.push_back(relative + ": <" + included->mPath + "> is in no folder of the order");
                        else if (target != folder && *to > *from)
                            found.push_back(relative + ": <" + included->mPath + "> comes after `" + folder
                                + "/` — move the file where what it includes allows, or split it");
                    }
                }

                for (const std::string_view folder : order.mOrder)
                    if (!met.contains(folder))
                        found.push_back(std::string(order.mRoot) + ": `" + std::string(folder)
                            + "` is in the order and holds no file — drop it");
            }

            EXPECT_TRUE(found.empty()) << joined(found);
        }

        /// The folders a `docs/architecture.md` table names, in its order: the first column of each row of the
        /// `table`th table whose header's first cell is `folder`, each name's backticks and trailing slash off.
        std::vector<std::string> documentedFolders(SourceTree& tree, const std::size_t table)
        {
            std::vector<std::string> folders;
            std::size_t met = 0;
            bool inside = false;
            for (const std::string& line : tree.linesOf(sRoot / "docs" / "architecture.md"))
            {
                if (!line.starts_with('|'))
                {
                    inside = false;
                    continue;
                }
                const std::size_t cellEnd = line.find('|', 1);
                std::string cell = line.substr(1, cellEnd == std::string::npos ? std::string::npos : cellEnd - 1);
                cell.erase(0, skipSpace(cell, 0));
                while (!cell.empty() && std::isspace(static_cast<unsigned char>(cell.back())))
                    cell.pop_back();
                if (cell == "folder")
                {
                    inside = met++ == table;
                    continue;
                }
                if (!inside || cell.size() < 3 || cell.front() != '`')
                    continue;
                std::string name = cell.substr(1, cell.find('`', 1) - 1);
                if (name.ends_with('/'))
                    name.pop_back();
                folders.push_back(name);
            }
            return folders;
        }

        /// **`docs/architecture.md` lists the core's and the backend's folders in the order this test holds**, its
        /// first two folder tables, every folder but the backend's root: a reader meets the layers there first.
        TEST_F(RtxSourceTreeTest, theArchitectureListsTheFoldersInTheirOrder)
        {
            for (std::size_t table = 0; table < 2; ++table)
            {
                const FolderOrder& order = sFolderOrders[table];
                std::vector<std::string> owed;
                for (const std::string_view folder : order.mOrder)
                    if (!folder.empty())
                        owed.emplace_back(folder);
                EXPECT_EQ(documentedFolders(*sTree, table), owed) << order.mRoot;
            }
        }

        /// **A header both languages read stands with the C++ that reads it.** The core's shader
        /// folder holds what core C++ reads, and a header that only the backend and its GLSL read is
        /// the backend's, in `components/rtxvulkan/shaders/shared/`, whatever it states: the GLSL
        /// that uses it lives in the backend already. And nothing of the core reads the backend.
        TEST_F(RtxSourceTreeTest, aSharedHeaderStandsWithTheCodeThatReadsIt)
        {
            const std::filesystem::path shaders = sCore / "shaders";

            std::set<std::string, std::less<>> read;
            std::vector<std::string> found;
            for (const std::filesystem::path& file : sTree->filesUnder(sCore))
            {
                const std::filesystem::path extension = file.extension();
                if (extension != ".cpp" && extension != ".hpp" && extension != ".h")
                    continue;

                for (const std::string& line : sTree->linesOf(file))
                {
                    const std::optional<Included> included = includedBy(line);
                    if (!included.has_value())
                        continue;

                    if (included->mPath.starts_with("components/rtxvulkan/"))
                        found.push_back(genericName(file.lexically_relative(sRoot)) + ": <" + included->mPath
                            + "> is the backend's");

                    const std::filesystem::path named = included->mQuoted
                        ? (file.parent_path() / included->mPath).lexically_normal()
                        : (sRoot / included->mPath).lexically_normal();
                    if (named.parent_path() == shaders && named != file)
                        read.insert(Files::pathToUnicodeString(named.filename()));
                }
            }

            EXPECT_FALSE(read.empty()) << "no file of the core reads a shared header, so this matches nothing";
            for (const std::filesystem::path& file : sTree->filesUnder(shaders))
            {
                const std::string name = Files::pathToUnicodeString(file.filename());
                if (file.extension() == ".h" && file.parent_path() == shaders && !read.contains(name))
                    found.push_back("components/rtx/shaders/" + name
                        + ": no file of the core reads it — it belongs in components/rtxvulkan/shaders/shared/");
            }

            EXPECT_TRUE(found.empty()) << joined(found);
        }

        /// The files `rtx/tests.cmake` names for `name`, a target its `target_sources` calls add to or a
        /// list its `set` makes, as paths: every call's, without the keywords and the variables a call
        /// names.
        std::vector<std::filesystem::path> listedIn(SourceTree& tree, const std::string_view name)
        {
            const std::string named = std::string(name);
            const std::set<std::string, std::less<>> keywords
                = { "PRIVATE", "PUBLIC", "INTERFACE", "FILE_SET", "HEADERS", "BASE_DIRS", "FILES" };

            // Whether `text` starts with the name, taking it off if so.
            const auto takesName = [&](std::string_view& text) {
                if (!text.starts_with(named))
                    return false;
                const std::string_view after = text.substr(named.size());
                if (!after.empty() && after.front() != ' ' && after.front() != ')')
                    return false;
                text = after;
                return true;
            };

            std::vector<std::filesystem::path> files;
            bool inside = false;
            // gersemi wraps a long call with its first argument on the line after the bare command.
            bool wrapped = false;
            for (const std::string& line : tree.linesOf(sTests / "rtx" / "tests.cmake"))
            {
                std::string_view rest = std::string_view(line).substr(skipSpace(line, 0));
                if (!inside)
                {
                    bool opens = false;
                    if (wrapped)
                        opens = takesName(rest);
                    wrapped = false;
                    for (const std::string_view command : { "set(", "target_sources(" })
                    {
                        if (rest == command)
                            wrapped = true;
                        else if (rest.starts_with(command))
                        {
                            std::string_view after = rest.substr(command.size());
                            if (takesName(after))
                            {
                                opens = true;
                                rest = after;
                            }
                        }
                    }
                    if (!opens)
                        continue;
                    inside = true;
                }

                const std::size_t close = rest.find(')');
                std::istringstream words{ std::string(rest.substr(0, close)) };
                for (std::string word; words >> word;)
                    if (!keywords.contains(word) && !word.starts_with('$') && !word.starts_with('"'))
                        files.push_back((sTests / word).lexically_normal());
                if (close != std::string_view::npos)
                    inside = false;
            }

            return files;
        }

        /// **Every file under the backend's shaders is named in its CMake file**: an entry shader left
        /// out of `RTX_SHADERS` is never built and fails only when a pass asks for it, and an
        /// include or a shared header left out is one the IDE lists nowhere. Named as `shaders/…`,
        /// which is how every list there spells it.
        TEST_F(RtxSourceTreeTest, everyShaderFileIsNamedInTheBackendsCMakeFile)
        {
            std::set<std::string, std::less<>> named;
            for (const std::string& line : sTree->linesOf(sBackend / "CMakeLists.txt"))
            {
                std::size_t at = 0;
                while ((at = line.find("shaders/", at)) != std::string::npos)
                {
                    const std::size_t end = line.find_first_of(" \t()\"", at);
                    named.insert(line.substr(at, end == std::string::npos ? std::string::npos : end - at));
                    at = end == std::string::npos ? line.size() : end;
                }
            }

            ASSERT_FALSE(named.empty()) << "the backend's CMake file names no shader";
            std::vector<std::string> found;
            for (const std::filesystem::path& file : sTree->filesUnder(sBackend / "shaders"))
            {
                const std::string relative = genericName(file.lexically_relative(sBackend));
                if (!named.contains(relative))
                    found.push_back(relative + " is named in no list of components/rtxvulkan/CMakeLists.txt");
            }

            EXPECT_TRUE(found.empty()) << joined(found);
        }

        /// **A test lives in the binary its needs decide.** A file of `rtx-gpu-tests` reaches the
        /// device's support and holds only tests over its fixtures, and a file of `components-tests`
        /// reaches none of it: CI runs the one and not the other, because hosted runners have no
        /// GPU, so a plain `TEST` among the device's files is a test CI never runs.
        TEST_F(RtxSourceTreeTest, everyTestIsInTheBinaryItsNeedsDecide)
        {
            const auto reachesDevice = [](const std::filesystem::path& file) {
                return std::ranges::any_of(sTree->reachedBy(file), [](const std::filesystem::path& one) {
                    return genericName(one).find("/rtxvulkan/support/device/") != std::string::npos;
                });
            };

            // What `rtx-gpu-tests` lists beside its tests: the shared `main`, and the support of both binaries
            // and of the device.
            const auto isSupport = [](const std::filesystem::path& file) {
                const std::string name = genericName(file);
                return file.filename() == "main.cpp" || name.find("/rtx/support/") != std::string::npos
                    || name.find("/rtxvulkan/support/") != std::string::npos;
            };

            std::vector<std::string> found;
            const std::vector<std::filesystem::path> device = listedIn(*sTree, "rtx-gpu-tests");
            ASSERT_FALSE(device.empty()) << "tests.cmake lists no files of rtx-gpu-tests: the target was renamed";
            for (const std::filesystem::path& file : device)
            {
                if (file.extension() != ".cpp" || isSupport(file))
                    continue;
                if (!reachesDevice(file))
                    found.push_back(
                        Files::pathToUnicodeString(file.filename()) + " opens no device: list it in components-tests");

                const std::vector<std::string>& lines = sTree->linesOf(file);
                for (std::size_t at = 0; at < lines.size(); ++at)
                {
                    const std::string_view code = codeOf(lines[at]).substr(skipSpace(lines[at], 0));
                    if (code.starts_with("TEST(") || code.starts_with("TEST_P("))
                        found.push_back(Files::pathToUnicodeString(file.filename()) + ':' + std::to_string(at + 1)
                            + ": a test over no device fixture, which CI never runs here");
                }
            }

            const std::vector<std::filesystem::path> host = listedIn(*sTree, "components-tests");
            ASSERT_FALSE(host.empty()) << "tests.cmake lists no files of components-tests: the target was renamed";
            for (const std::string_view list : { "components-tests", "RTX_TEST_SUPPORT", "RTX_TEST_SUPPORT_HEADERS" })
                for (const std::filesystem::path& file : listedIn(*sTree, list))
                    if (reachesDevice(file))
                        found.push_back(Files::pathToUnicodeString(file.filename())
                            + " reaches the device's support: list it in rtx-gpu-tests");

            EXPECT_TRUE(found.empty()) << joined(found);
        }
    }
}
