#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace Rtx
{
    namespace
    {
        /// What the tree says about itself, asked of the sources rather than of a build, so a rule
        /// holds on every run and not only when somebody remembers to check it.
        const std::filesystem::path sRoot{ OPENMW_PROJECT_SOURCE_DIR };
        const std::filesystem::path sBackend = sRoot / "components" / "rtxvulkan";

        std::vector<std::string> linesOf(const std::filesystem::path& file)
        {
            std::ifstream in(file);
            std::vector<std::string> lines;
            for (std::string line; std::getline(in, line);)
                lines.push_back(line);
            return lines;
        }

        bool isWordStart(const char c)
        {
            return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
        }

        bool isWordChar(const char c)
        {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        /// The identifier starting at `at`, or empty where none does; `at` is moved past it.
        std::string_view wordAt(const std::string_view text, std::size_t& at)
        {
            if (at >= text.size() || !isWordStart(text[at]))
                return {};

            const std::size_t start = at;
            while (at < text.size() && isWordChar(text[at]))
                ++at;
            return text.substr(start, at - start);
        }

        std::size_t skipSpace(const std::string_view text, std::size_t at)
        {
            while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])))
                ++at;
            return at;
        }

        void wordsOf(const std::string_view text, std::set<std::string, std::less<>>& into)
        {
            for (std::size_t at = 0; at < text.size();)
            {
                if (isWordStart(text[at]) && (at == 0 || !isWordChar(text[at - 1])))
                    into.emplace(wordAt(text, at));
                else
                    ++at;
            }
        }

        std::string_view codeOf(const std::string_view line)
        {
            return line.substr(0, line.find("//"));
        }

        std::string_view commentOf(const std::string_view line)
        {
            const std::size_t at = line.find("//");
            return at == std::string_view::npos ? std::string_view() : line.substr(at);
        }

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

        /// Every line `match` accepts of every `.cpp` and `.hpp` anywhere under `places`, as
        /// `file:line: code`, the files named in `exempt` left out. Comments are stripped before
        /// the match, because a rule's own prose and the comments that explain a site name what
        /// the rule looks for.
        template <class Match>
        std::vector<std::string> linesMatching(const std::initializer_list<std::filesystem::path> places,
            const std::set<std::string>& exempt, const Match match)
        {
            std::vector<std::string> found;
            for (const std::filesystem::path& place : places)
            {
                for (const std::filesystem::directory_entry& entry :
                    std::filesystem::recursive_directory_iterator(place))
                {
                    const std::filesystem::path& file = entry.path();
                    if (file.extension() != ".cpp" && file.extension() != ".hpp")
                        continue;
                    if (exempt.contains(file.filename().string()))
                        continue;

                    const std::vector<std::string> lines = linesOf(file);
                    for (std::size_t at = 0; at < lines.size(); ++at)
                    {
                        const std::string_view code = codeOf(lines[at]);
                        if (match(code))
                            found.push_back(
                                file.filename().string() + ':' + std::to_string(at + 1) + ": " + std::string(code));
                    }
                }
            }

            return found;
        }

        /// Every Vulkan handle the backend owns is held by `Rtx::Owned`.
        ///
        /// **The rule is mechanical, so a test keeps it.** `owned.hpp` is "the one place
        /// `vkDestroyX(device, handle, allocator)` is spelled", and a class that spells it itself pays
        /// a destructor, a null check and a `const Device&` member that is there for the destructor to
        /// reach the device. A loaded destroyer (`mDestroyX`) is matched as well as a declared one.
        ///
        /// The exemptions, each for a reason a match cannot see: `vkDestroyInstance` and
        /// `vkDestroyDevice` take no parent handle, so `Owned`'s shape does not fit them;
        /// the surface (`Surface`'s `mDestroySurface`) and the messenger take the instance
        /// rather than the device, two sites not worth a second template parameter;
        /// `accelerationstructure.cpp` destroys through a pointer the device loaded, which `Owned`'s
        /// template argument cannot name, so it is the `Owned` for that handle and buries it the
        /// same way.
        TEST(RtxSourceTreeTest, everyDeviceParentedVulkanHandleIsHeldByOwned)
        {
            const std::set<std::string> exemptFiles{ "owned.hpp", "accelerationstructure.cpp" };
            const std::set<std::string> allowed{ "vkDestroyInstance", "vkDestroyDevice", "mDestroySurface",
                "vkDestroyDebugUtilsMessengerEXT", "mDestroyMessenger" };

            const std::vector<std::string> found
                = linesMatching({ sBackend }, exemptFiles, [&](const std::string_view code) {
                      const std::optional<std::string> call = destroyCallOn(code);
                      return call.has_value() && !allowed.contains(*call);
                  });

            EXPECT_TRUE(found.empty())
                << "a Vulkan handle is destroyed by hand where Rtx::Owned would do it — hold it "
                   "as Owned<Handle, vkDestroyX> and delete the destructor, or add the site to the "
                   "exemptions above with the reason it cannot be one:\n"
                << joined(found);
        }

        /// A handle ended at once is held by `Rtx::Immediate` only where the graveyard cannot
        /// outlive it.
        ///
        /// **Everything else is `Owned`, which buries**, so a handle a submit may still read is never
        /// ended under it. The three outside it say why where they are declared: the clock's
        /// semaphore and the pipeline cache, which the device takes apart after its graveyard, and
        /// the swapchain, whose surface goes first.
        TEST(RtxSourceTreeTest, onlyWhatOutlivesTheGraveyardIsEndedAtOnce)
        {
            const std::set<std::string> allowed{ "owned.hpp", "handles.hpp", "handles.cpp", "timeline.hpp",
                "pipelinecache.hpp", "swapchain.hpp", "swapchain.cpp" };

            const std::vector<std::string> found = linesMatching({ sBackend }, allowed,
                [](const std::string_view code) { return code.find("Immediate<") != std::string_view::npos; });

            EXPECT_TRUE(found.empty())
                << "a handle is ended at once where Rtx::Owned would bury it — hold it as Owned, or add "
                   "the file above with the reason the graveyard cannot outlive it:\n"
                << joined(found);
        }

        /// A scene slot a renderer hands out is held by `Rtx::ViewScene`, which gives it back.
        ///
        /// **A take in a constructor and a drop in a destructor are two classes and a throw apart**:
        /// a constructor that unwinds after the take leaks the slot for the renderer's life, and a
        /// drop of a slot already dropped puts it on the free list twice. The handle is the one place
        /// the pair is spelled, so a picture that wants a scene holds one and cannot get it wrong.
        TEST(RtxSourceTreeTest, everyViewSceneIsHeldByViewScene)
        {
            const std::set<std::string> allowed{ "viewscene.cpp", "renderer.hpp", "vulkanrenderer.hpp",
                "vulkanrenderer.cpp" };

            const std::vector<std::string> found
                = linesMatching({ sRoot / "components" / "rtx", sBackend,
                                    sRoot / "apps" / "openmw" / "mwrender" / "rtx", sRoot / "apps" / "rtxtool" },
                    allowed, [](const std::string_view code) {
                        return code.find("addViewScene(") != std::string_view::npos
                            || code.find("dropViewScene(") != std::string_view::npos;
                    });

            EXPECT_TRUE(found.empty())
                << "a view scene is taken or dropped by hand where Rtx::ViewScene would do it — hold "
                   "one of those and delete the drop:\n"
                << joined(found);
        }

        /// A death the fork's tests assert goes through `Testing::expectDies`, whose child keeps no
        /// core: a bare one writes a core to the journal on every run.
        TEST(RtxSourceTreeTest, everyDeathIsAssertedWithoutACore)
        {
            const std::filesystem::path tests = sRoot / "apps" / "components_tests";
            const std::set<std::string> exempt{ "death.hpp", "sourcetree.cpp" };

            const std::vector<std::string> found
                = linesMatching({ tests / "rtx", tests / "rtxvulkan", tests / "myguirtx", tests / "rtxtool",
                                    sRoot / "apps" / "openmw_tests" / "mwrender" },
                    exempt, [](const std::string_view code) {
                        return code.find("_DEATH(") != std::string_view::npos
                            || code.find("_EXIT(") != std::string_view::npos;
                    });

            EXPECT_TRUE(found.empty()) << "a death is asserted by hand where Testing::expectDies would keep its core "
                                          "out of the journal:\n"
                                       << joined(found);
        }

        /// Where a quoted `#include` is looked for when the including file's folder does not hold
        /// it: the two roots the shader build passes with `-I`.
        const std::array<std::filesystem::path, 2> sShaderRoots{ sBackend / "shaders",
            sRoot / "components" / "rtx" / "shaders" };

        /// Where a quoted `#include` is found, or nothing for one outside the tree: beside the
        /// including file, then under a shader root.
        std::optional<std::filesystem::path> quotedTarget(const std::filesystem::path& from, const std::string& path)
        {
            if (std::filesystem::path beside = from.parent_path() / path; std::filesystem::exists(beside))
                return beside;
            for (const std::filesystem::path& root : sShaderRoots)
                if (std::filesystem::path under = root / path; std::filesystem::exists(under))
                    return under;
            return std::nullopt;
        }

        /// Every file `file` reaches through `#include`, itself included, by the paths the sources
        /// spell: a quoted one as `quotedTarget` finds it, and one in angle brackets from the source
        /// root where it names `apps/`. Not `components/`, which reaches upstream's whole graph and
        /// nothing a rule here asks about.
        void reachedBy(const std::filesystem::path& file, std::set<std::filesystem::path>& reached)
        {
            const std::filesystem::path normal = file.lexically_normal();
            if (!reached.insert(normal).second)
                return;

            for (const std::string& line : linesOf(normal))
            {
                const std::optional<Included> included = includedBy(line);
                if (!included.has_value())
                    continue;

                if (included->mQuoted)
                {
                    if (const std::optional<std::filesystem::path> named = quotedTarget(normal, included->mPath))
                        reachedBy(*named, reached);
                }
                else if (included->mPath.starts_with("apps/"))
                {
                    if (const std::filesystem::path named = sRoot / included->mPath; std::filesystem::exists(named))
                        reachedBy(named, reached);
                }
            }
        }

        /// No program links the Vulkan loader: volk loads it where an instance is made
        /// (`openmw-rtx-vulkan-api`).
        ///
        /// **A program that imports the loader does not start without it**, the OpenGL renderer
        /// included, and on Windows one that imports a function an old loader lacks stops in a
        /// dialog before `main`. `VK_NO_PROTOTYPES` keeps every call that bypasses volk from
        /// compiling, so a link of the import library is the one way back, and this finds it.
        TEST(RtxSourceTreeTest, noProgramLinksTheVulkanLoader)
        {
            std::vector<std::string> found;
            const auto scan = [&](const std::filesystem::path& file) {
                const std::vector<std::string> lines = linesOf(file);
                for (std::size_t at = 0; at < lines.size(); ++at)
                {
                    const std::string_view code = std::string_view(lines[at]).substr(0, lines[at].find('#'));
                    if (code.find("Vulkan::Vulkan") != std::string_view::npos)
                        found.push_back(std::filesystem::relative(file, sRoot).string() + ':' + std::to_string(at + 1)
                            + ": " + lines[at]);
                }
            };

            scan(sRoot / "CMakeLists.txt");
            for (const char* const place : { "apps", "cmake", "components", "extern" })
                for (const std::filesystem::directory_entry& entry :
                    std::filesystem::recursive_directory_iterator(sRoot / place))
                    if (entry.path().filename() == "CMakeLists.txt" || entry.path().extension() == ".cmake")
                        scan(entry.path());

            EXPECT_TRUE(found.empty()) << "a target links the Vulkan loader; link `openmw-rtx-vulkan-api`:\n"
                                       << joined(found);
        }

        /// No compute shader traces a ray.
        ///
        /// **A ray query inside a compute dispatch answers differently from run to run** — a
        /// candidate counted twice or not at all — while another process shares the card, and never
        /// inside a ray-tracing launch. So what is asked here, of every `.comp` and everything it
        /// includes, is whether it reaches `rayQueryEXT`.
        TEST(RtxSourceTreeTest, noComputeShaderTracesARay)
        {
            const std::filesystem::path shaders = sBackend / "shaders";

            std::vector<std::string> found;
            for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(shaders))
            {
                if (entry.path().extension() != ".comp")
                    continue;

                std::set<std::filesystem::path> reached;
                reachedBy(entry.path(), reached);

                for (const std::filesystem::path& file : reached)
                {
                    const std::vector<std::string> lines = linesOf(file);
                    const bool queries = std::any_of(lines.begin(), lines.end(),
                        [](const std::string& line) { return line.find("rayQueryEXT") != std::string::npos; });
                    if (queries)
                        found.push_back(entry.path().filename().string() + " reaches "
                            + std::filesystem::relative(file, shaders).string());
                }
            }

            EXPECT_TRUE(found.empty()) << "a compute shader traces a ray, which answers differently under another "
                                          "process's preemption — make the pass a launch:\n"
                                       << joined(found);
        }

        /// One library's folders in the order they may include each other: a folder includes only
        /// the folders before it, and a folder inside one of them is a part of it, which the two may
        /// both reach into. `""` is a file straight under the library's root.
        struct FolderOrder
        {
            std::string_view mLibrary;
            std::vector<std::string_view> mOrder;
        };

        const std::array<FolderOrder, 2> sFolderOrders{
            FolderOrder{ "rtx",
                { "shaders", "common", "image", "preprocess", "scene", "frame", "renderer", "mirror", "environment",
                    "view" } },
            FolderOrder{ "rtxvulkan",
                { "spirv", "device", "pipeline", "texture", "scene", "trace", "upscale", "display", "present", "gui",
                    "" } },
        };

        /// The folder of the library a path under its root stands in: the first of its names, or
        /// `""` for a file straight under the root.
        std::string topFolderOf(const std::filesystem::path& relative)
        {
            const auto first = relative.begin();
            return std::next(first) == relative.end() ? std::string() : first->string();
        }

        /// **A folder includes only the folders before it**, which is what the folders are: a layer
        /// each, so that what a file may reach is read off where it stands. A quoted include names a
        /// header of the file's own folder, and any other is spelled from the root, so the direction
        /// is read off every line that crosses a folder.
        TEST(RtxSourceTreeTest, everyFolderIncludesOnlyTheFoldersBeforeIt)
        {
            std::vector<std::string> found;
            for (const FolderOrder& library : sFolderOrders)
            {
                const std::filesystem::path root = sRoot / "components" / library.mLibrary;
                const std::string rooted = "components/" + std::string(library.mLibrary) + '/';
                const auto rankOf = [&](const std::string_view folder) -> std::optional<std::size_t> {
                    const auto at = std::find(library.mOrder.begin(), library.mOrder.end(), folder);
                    if (at == library.mOrder.end())
                        return std::nullopt;
                    return static_cast<std::size_t>(at - library.mOrder.begin());
                };

                for (const std::filesystem::directory_entry& entry :
                    std::filesystem::recursive_directory_iterator(root))
                {
                    const std::filesystem::path& file = entry.path();
                    if (file.extension() != ".cpp" && file.extension() != ".hpp")
                        continue;

                    const std::string relative = file.lexically_relative(sRoot).generic_string();
                    const std::string folder = topFolderOf(file.lexically_relative(root));
                    const std::optional<std::size_t> from = rankOf(folder);
                    if (!from.has_value())
                    {
                        found.push_back(relative + ": its folder is in no order — name it in `sFolderOrders`");
                        continue;
                    }

                    for (const std::string& line : linesOf(file))
                    {
                        const std::optional<Included> included = includedBy(line);
                        if (!included.has_value())
                            continue;

                        if (included->mQuoted)
                        {
                            if (!std::filesystem::exists(file.parent_path() / included->mPath))
                                found.push_back(relative + ": \"" + included->mPath
                                    + "\" is not in its own folder — spell it from the root");
                            continue;
                        }

                        if (!included->mPath.starts_with(rooted))
                            continue;

                        const std::string target = topFolderOf(included->mPath.substr(rooted.size()));
                        const std::optional<std::size_t> to = rankOf(target);
                        if (!to.has_value())
                            found.push_back(relative + ": <" + included->mPath + "> is in no folder of the order");
                        else if (target != folder && *to > *from)
                            found.push_back(relative + ": <" + included->mPath + "> comes after `" + folder
                                + "/` — move the file where what it includes allows, or split it");
                    }
                }
            }

            EXPECT_TRUE(found.empty()) << joined(found);
        }

        /// The files `rtx/tests.cmake` names in its list `list`, as paths.
        std::vector<std::filesystem::path> listedIn(const std::string_view list)
        {
            const std::filesystem::path tests = sRoot / "apps" / "components_tests";
            const std::string opening = "set(" + std::string(list);

            std::vector<std::filesystem::path> files;
            bool inside = false;
            for (const std::string& line : linesOf(tests / "rtx" / "tests.cmake"))
            {
                const std::string_view entry = std::string_view(line).substr(skipSpace(line, 0));
                if (!inside)
                    inside = entry == opening;
                else if (entry.starts_with(')'))
                    break;
                else if (!entry.empty())
                    files.push_back(tests / entry);
            }

            return files;
        }

        /// **A test lives in the binary its needs decide.** A file of `rtx-gpu-tests` reaches the
        /// device's support and holds only tests over its fixtures, and a file of `components-tests`
        /// reaches none of it: CI runs the one and not the other, because hosted runners have no
        /// GPU, so a plain `TEST` among the device's files is a test CI never runs.
        TEST(RtxSourceTreeTest, everyTestIsInTheBinaryItsNeedsDecide)
        {
            const auto reachesDevice = [](const std::filesystem::path& file) {
                std::set<std::filesystem::path> reached;
                reachedBy(file, reached);
                return std::any_of(reached.begin(), reached.end(), [](const std::filesystem::path& one) {
                    return one.generic_string().find("/rtx/support/device/") != std::string::npos;
                });
            };

            std::vector<std::string> found;
            const std::vector<std::filesystem::path> device = listedIn("RTX_GPU_TEST_FILES");
            ASSERT_FALSE(device.empty()) << "tests.cmake lists no device tests: the list was renamed";
            for (const std::filesystem::path& file : device)
            {
                if (file.extension() != ".cpp")
                    continue;
                if (!reachesDevice(file))
                    found.push_back(file.filename().string() + " opens no device: list it in RTX_TEST_FILES");

                const std::vector<std::string> lines = linesOf(file);
                for (std::size_t at = 0; at < lines.size(); ++at)
                {
                    const std::string_view code = codeOf(lines[at]).substr(skipSpace(lines[at], 0));
                    if (code.starts_with("TEST(") || code.starts_with("TEST_P("))
                        found.push_back(file.filename().string() + ':' + std::to_string(at + 1)
                            + ": a test over no device fixture, which CI never runs here");
                }
            }

            for (const std::string_view list : { "RTX_TEST_FILES", "RTX_TEST_SUPPORT" })
                for (const std::filesystem::path& file : listedIn(list))
                    if (reachesDevice(file))
                        found.push_back(file.filename().string() + " reaches the device's support: list it in "
                                                                   "RTX_GPU_TEST_FILES");

            EXPECT_TRUE(found.empty()) << joined(found);
        }

        /// The fork's own directories, every file of which the index reads and the rules check.
        constexpr std::array<std::string_view, 5> sForkDirectories{ "components/rtx", "components/rtxvulkan",
            "components/myguirtx", "apps/openmw/mwrender/rtx", "apps/rtxtool" };

        /// The namespaces a comment's `A::b` is read under as a name the code declares somewhere.
        const std::set<std::string, std::less<>> sForkNamespaces{ "Rtx", "MWRender", "MyGUIRtx", "RtxTool", "Shaders" };

        bool isSource(const std::filesystem::path& file)
        {
            static const std::set<std::string, std::less<>> sExtensions{ ".cpp", ".hpp", ".h", ".glsl", ".comp",
                ".rgen", ".rchit", ".rahit", ".rmiss", ".vert", ".frag" };
            return sExtensions.contains(file.extension().string());
        }

        /// Every span between two backticks on `line`.
        std::vector<std::string_view> quotedOn(const std::string_view line)
        {
            std::vector<std::string_view> spans;
            for (std::size_t open = line.find('`'); open != std::string_view::npos;)
            {
                const std::size_t close = line.find('`', open + 1);
                if (close == std::string_view::npos)
                    break;
                spans.push_back(line.substr(open + 1, close - open - 1));
                open = line.find('`', close + 1);
            }
            return spans;
        }

        std::vector<std::filesystem::path> forkDocuments()
        {
            std::vector<std::filesystem::path> documents;
            for (const std::filesystem::path& directory : { sRoot / "docs" / "rtx", sRoot / "components" / "rtx" })
                for (const std::filesystem::directory_entry& entry :
                    std::filesystem::recursive_directory_iterator(directory))
                    if (entry.path().extension() == ".md")
                        documents.push_back(entry.path());
            return documents;
        }

        /// One source file as the rules read it.
        struct Source
        {
            std::filesystem::path mPath;
            std::vector<std::string> mLines;
            bool mFork = false;
            std::set<std::string, std::less<>> mWords;
        };

        /// What the code of the fork and of upstream's `mwrender` declares: every word, and which
        /// files define each class, struct and enum with a body, with the bases each names. Read
        /// once for the two rules that ask the code, comments left out, because a rule's own prose
        /// and a stale comment both name what the code does not.
        struct SourceIndex
        {
            std::vector<Source> mSources;
            std::set<std::string, std::less<>> mWords;
            std::map<std::string, std::vector<const Source*>, std::less<>> mDefinitions;
            std::map<std::string, std::set<std::string>, std::less<>> mBases;

            /// The types a file of the fork defines, which are the ones a comment's `Type::member`
            /// is checked against.
            std::set<std::string, std::less<>> mOurs;

            /// What a type and its bases declare, from every file that defines one of them under
            /// the name and the source beside each. Every definition under the name, because the
            /// fork and upstream share some — `Renderer`, `Camera`, `Moon` — and a comment names
            /// either.
            const std::set<std::string, std::less<>>& membersOf(std::string_view type) const;

        private:
            void addMembers(std::string_view type, std::set<std::string, std::less<>>& into,
                std::set<std::string, std::less<>>& seen) const;

            /// `membersOf`'s answers, kept because the rule asks about one type many times.
            mutable std::map<std::string, std::set<std::string, std::less<>>, std::less<>> mMembers;
        };

        void SourceIndex::addMembers(std::string_view type, std::set<std::string, std::less<>>& into,
            std::set<std::string, std::less<>>& seen) const
        {
            if (!seen.emplace(type).second)
                return;

            if (const auto found = mDefinitions.find(type); found != mDefinitions.end())
                for (const Source* source : found->second)
                {
                    into.insert(source->mWords.begin(), source->mWords.end());
                    for (const std::string_view twin : { ".cpp", ".hpp" })
                    {
                        std::filesystem::path path = source->mPath;
                        path.replace_extension(twin);
                        for (const Source& other : mSources)
                            if (other.mPath == path)
                                into.insert(other.mWords.begin(), other.mWords.end());
                    }
                }

            if (const auto found = mBases.find(type); found != mBases.end())
                for (const std::string& base : found->second)
                    addMembers(base, into, seen);
        }

        const std::set<std::string, std::less<>>& SourceIndex::membersOf(const std::string_view type) const
        {
            if (const auto known = mMembers.find(type); known != mMembers.end())
                return known->second;

            std::set<std::string, std::less<>> members;
            std::set<std::string, std::less<>> seen;
            addMembers(type, members, seen);
            return mMembers.emplace(std::string(type), std::move(members)).first->second;
        }

        /// The definitions in `code`: `class`, `struct` or `enum` and a name followed by a body or
        /// a base list, and never by `;` — a declaration — or by `>` and `,`, a template parameter.
        void readDefinitions(const std::string_view code, const Source& source, SourceIndex& index)
        {
            for (std::size_t at = 0; at < code.size();)
            {
                if (!isWordStart(code[at]) || (at > 0 && isWordChar(code[at - 1])))
                {
                    ++at;
                    continue;
                }

                const std::string_view keyword = wordAt(code, at);
                if (keyword != "class" && keyword != "struct" && keyword != "enum")
                    continue;

                std::size_t next = skipSpace(code, at);
                std::string_view name = wordAt(code, next);
                if (keyword == "enum" && (name == "class" || name == "struct"))
                {
                    next = skipSpace(code, next);
                    name = wordAt(code, next);
                }
                if (name.empty())
                    continue;

                std::size_t after = skipSpace(code, next);
                if (std::size_t word = after; wordAt(code, word) == "final")
                    after = skipSpace(code, word);
                if (after >= code.size() || (code[after] != '{' && code[after] != ':'))
                    continue;
                if (code.substr(after, 2) == "::")
                    continue;

                if (code[after] == ':')
                {
                    const std::size_t body = code.find('{', after);
                    const std::size_t end = code.find(';', after);
                    if (body == std::string_view::npos || end < body)
                        continue;

                    const std::string_view list = code.substr(after + 1, body - after - 1);
                    std::string last;
                    for (std::size_t in = 0; in < list.size();)
                    {
                        const std::string_view word = wordAt(list, in);
                        if (word.empty())
                        {
                            if (!last.empty() && list[in] != ':')
                            {
                                index.mBases[std::string(name)].insert(last);
                                last.clear();
                            }
                            ++in;
                        }
                        else if (word != "public" && word != "private" && word != "protected" && word != "virtual")
                            last = word;
                    }
                    if (!last.empty())
                        index.mBases[std::string(name)].insert(last);
                }

                index.mDefinitions[std::string(name)].push_back(&source);
                if (source.mFork)
                    index.mOurs.emplace(name);
                at = next;
            }
        }

        const SourceIndex& sourceIndex()
        {
            static const SourceIndex sIndex = [] {
                SourceIndex index;
                const auto read = [&](const std::filesystem::path& file, const bool fork) {
                    Source& source = index.mSources.emplace_back();
                    source.mPath = file;
                    source.mLines = linesOf(file);
                    source.mFork = fork;
                };

                for (const std::string_view directory : sForkDirectories)
                    for (const std::filesystem::directory_entry& entry :
                        std::filesystem::recursive_directory_iterator(sRoot / directory))
                        if (entry.is_regular_file() && isSource(entry.path()))
                            read(entry.path(), true);

                // Upstream's `mwrender` too, straight under it, because the fork's comments name its
                // types beside the fork's own.
                for (const std::filesystem::directory_entry& entry :
                    std::filesystem::directory_iterator(sRoot / "apps" / "openmw" / "mwrender"))
                    if (entry.is_regular_file() && isSource(entry.path()))
                        read(entry.path(), false);

                // Addresses into `mSources` are taken below, once it has stopped growing.
                for (Source& source : index.mSources)
                {
                    std::string code;
                    for (const std::string& line : source.mLines)
                        (code += codeOf(line)) += '\n';

                    wordsOf(code, source.mWords);
                    index.mWords.insert(source.mWords.begin(), source.mWords.end());
                    readDefinitions(code, source, index);
                }

                return index;
            }();

            return sIndex;
        }

        /// Where a finding is: the file under the source root, and the line.
        std::string placeOf(const std::filesystem::path& file, const std::size_t line)
        {
            return std::filesystem::relative(file, sRoot).generic_string() + ':' + std::to_string(line + 1);
        }

        /// Every `@param` names a parameter of what its comment documents.
        ///
        /// **A parameter renamed or removed leaves its line in the comment**, and the comment then
        /// describes an argument nobody passes. The names are read off each block of `///` lines,
        /// and the declaration is what follows it up to the line that ends in `;`, `{` or `}`.
        TEST(RtxSourceTreeTest, everyParamDocNamesAParameter)
        {
            std::vector<std::string> found;
            for (const Source& source : sourceIndex().mSources)
            {
                if (!source.mFork)
                    continue;

                const std::vector<std::string>& lines = source.mLines;
                const auto isDoc = [&](const std::size_t line) {
                    const std::size_t first = skipSpace(lines[line], 0);
                    return std::string_view(lines[line]).substr(first).starts_with("///");
                };
                for (std::size_t at = 0; at < lines.size();)
                {
                    if (!isDoc(at))
                    {
                        ++at;
                        continue;
                    }

                    const std::size_t start = at;
                    std::vector<std::string> names;
                    for (; at < lines.size() && isDoc(at); ++at)
                    {
                        const std::string_view line = lines[at];
                        for (std::size_t tag = line.find("@param"); tag != std::string_view::npos;
                             tag = line.find("@param", tag + 1))
                        {
                            std::size_t in = skipSpace(line, tag + 6);
                            for (std::string_view name = wordAt(line, in); !name.empty();)
                            {
                                names.emplace_back(name);
                                in = skipSpace(line, in);
                                if (in >= line.size() || line[in] != ',')
                                    break;
                                in = skipSpace(line, in + 1);
                                name = wordAt(line, in);
                            }
                        }
                    }

                    if (names.empty())
                        continue;

                    std::set<std::string, std::less<>> declared;
                    for (std::size_t line = at; line < lines.size(); ++line)
                    {
                        const std::string_view code = codeOf(lines[line]);
                        wordsOf(code, declared);
                        const std::size_t end = code.find_last_not_of(" \t\r");
                        if (end != std::string_view::npos && (code[end] == ';' || code[end] == '{' || code[end] == '}'))
                            break;
                    }

                    for (const std::string& name : names)
                        if (!declared.contains(name))
                            found.push_back(placeOf(source.mPath, start) + ": @param " + name);
                }
            }

            EXPECT_TRUE(found.empty()) << "a parameter doc names a parameter the declaration under it does not have "
                                          "— rename it or delete the line:\n"
                                       << joined(found);
        }

        /// Every member a comment names in backticks is declared.
        ///
        /// **A name in a comment is a claim nothing compiles.** A member renamed or removed leaves
        /// every comment that named it pointing at nothing, and the reader who follows one finds
        /// nothing there. Two
        /// readings, both of the code and never of a comment: `Ns::name` under a namespace of the
        /// fork's names something the code declares somewhere, and `Type::member` for a type a file
        /// of the fork defines names something that type, a base of it or their sources declare.
        /// A name another namespace qualifies is not the fork's to check, a name followed by `*`
        /// is a pattern, and a destructor is its type's. The fork's documents are read too.
        TEST(RtxSourceTreeTest, everyMemberACommentNamesIsDeclared)
        {
            const SourceIndex& index = sourceIndex();

            std::vector<std::string> found;
            const auto check
                = [&](const std::filesystem::path& file, const std::size_t line, const std::string_view text) {
                      for (const std::string_view span : quotedOn(text))
                      {
                          for (std::size_t at = 0; at < span.size();)
                          {
                              if (!isWordStart(span[at]) || (at > 0 && isWordChar(span[at - 1])))
                              {
                                  ++at;
                                  continue;
                              }

                              std::vector<std::string_view> parts{ wordAt(span, at) };
                              while (span.substr(at, 2) == "::")
                              {
                                  std::size_t next = at + 2;
                                  if (next < span.size() && span[next] == '~')
                                      ++next;
                                  const std::string_view part = wordAt(span, next);
                                  if (part.empty())
                                      break;
                                  parts.push_back(span[at + 2] == '~' ? std::string_view() : part);
                                  at = next;
                              }

                              if (parts.size() < 2 || (at < span.size() && span[at] == '*'))
                                  continue;
                              if (parts.size() > 2 && !sForkNamespaces.contains(parts.front()))
                                  continue;

                              const std::string_view owner = parts[parts.size() - 2];
                              const std::string_view member = parts.back();
                              if (member.empty())
                                  continue;

                              const bool missing = sForkNamespaces.contains(owner)
                                  ? !index.mWords.contains(member)
                                  : index.mOurs.contains(owner) && !index.membersOf(owner).contains(member);
                              if (missing)
                                  found.push_back(
                                      placeOf(file, line) + ": " + std::string(owner) + "::" + std::string(member));
                          }
                      }
                  };

            for (const Source& source : index.mSources)
                if (source.mFork)
                    for (std::size_t line = 0; line < source.mLines.size(); ++line)
                        check(source.mPath, line, commentOf(source.mLines[line]));

            for (const std::filesystem::path& document : forkDocuments())
            {
                const std::vector<std::string> lines = linesOf(document);
                for (std::size_t line = 0; line < lines.size(); ++line)
                    check(document, line, lines[line]);
            }

            EXPECT_TRUE(found.empty()) << "a comment names a member the code does not declare — name what is "
                                          "there now:\n"
                                       << joined(found);
        }

        /// Every link and every rooted path the fork's documents name resolves.
        ///
        /// **A document moved or deleted leaves every link to it.** A link resolves
        /// from the document's own folder; a path in backticks that starts at one of the tree's
        /// top folders resolves from the root, and one with a `*` or a space in it is a pattern or
        /// a sentence.
        TEST(RtxSourceTreeTest, everyLinkAndPathTheDocumentsNameResolves)
        {
            constexpr std::array<std::string_view, 6> tops{ "components/", "apps/", "docs/", "files/", "CI/",
                "cmake/" };

            std::vector<std::string> found;
            for (const std::filesystem::path& document : forkDocuments())
            {
                const std::vector<std::string> lines = linesOf(document);
                for (std::size_t line = 0; line < lines.size(); ++line)
                {
                    const std::string_view text = lines[line];
                    for (std::size_t link = text.find("]("); link != std::string_view::npos;
                         link = text.find("](", link + 2))
                    {
                        const std::size_t close = text.find(')', link + 2);
                        if (close == std::string_view::npos)
                            break;

                        const std::string_view target = text.substr(link + 2, close - link - 2);
                        if (target.starts_with("http://") || target.starts_with("https://") || target.starts_with('#'))
                            continue;

                        const std::string_view file = target.substr(0, target.find('#'));
                        if (!std::filesystem::exists(document.parent_path() / file))
                            found.push_back(placeOf(document, line) + ": " + std::string(target));
                    }

                    for (const std::string_view span : quotedOn(text))
                    {
                        const bool rooted = std::any_of(tops.begin(), tops.end(),
                            [&](const std::string_view top) { return span.starts_with(top); });
                        if (!rooted || span.find_first_of("* ") != std::string_view::npos)
                            continue;

                        if (!std::filesystem::exists(sRoot / span))
                            found.push_back(placeOf(document, line) + ": " + std::string(span));
                    }
                }
            }

            EXPECT_TRUE(found.empty()) << "a document names a file that is not there:\n" << joined(found);
        }
    }
}
