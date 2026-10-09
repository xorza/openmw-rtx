#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <set>
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
        const std::filesystem::path sBackend = sRoot / "components" / "rtxvulkan";

        std::vector<std::string> linesOf(const std::filesystem::path& file)
        {
            std::ifstream in(file);
            std::vector<std::string> lines;
            for (std::string line; std::getline(in, line);)
                lines.push_back(line);
            return lines;
        }

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
                    if (exempt.contains(Files::pathToUnicodeString(file.filename())))
                        continue;

                    const std::vector<std::string> lines = linesOf(file);
                    for (std::size_t at = 0; at < lines.size(); ++at)
                    {
                        const std::string_view code = codeOf(lines[at]);
                        if (match(code))
                            found.push_back(Files::pathToUnicodeString(file.filename()) + ':' + std::to_string(at + 1)
                                + ": " + std::string(code));
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
            const std::set<std::string> allowed{ "owned.hpp", "timeline.hpp", "timeline.cpp", "pipelinecache.hpp",
                "pipelinecache.cpp", "swapchain.hpp", "swapchain.cpp" };

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
                        found.push_back(Files::pathToUnicodeString(std::filesystem::relative(file, sRoot)) + ':'
                            + std::to_string(at + 1) + ": " + lines[at]);
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
                        found.push_back(Files::pathToUnicodeString(entry.path().filename()) + " reaches "
                            + Files::pathToUnicodeString(std::filesystem::relative(file, shaders)));
                }
            }

            EXPECT_TRUE(found.empty()) << "a compute shader traces a ray, which answers differently under another "
                                          "process's preemption — make the pass a launch:\n"
                                       << joined(found);
        }

        /// **Every store of an image goes through the census** (`census.glsl`): `RTX_STORE_COUNTED` for
        /// a float, which counts it, and `RTX_STORE_WORDS` for words, whose packing counted the
        /// floats it took. A raw `imageStore` is a store a NaN crosses uncounted. The probes are the
        /// tests' instruments, which store what a test hands them and run in no frame.
        TEST(RtxSourceTreeTest, everyImageStoreGoesThroughTheCensus)
        {
            const std::filesystem::path shaders = sBackend / "shaders";
            const std::filesystem::path census = shaders / "lib" / "census.glsl";

            std::vector<std::string> found;
            for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(shaders))
            {
                const std::filesystem::path& file = entry.path();
                if (!entry.is_regular_file() || file == census || file.parent_path() == shaders / "probes")
                    continue;

                const std::vector<std::string> lines = linesOf(file);
                for (std::size_t at = 0; at < lines.size(); ++at)
                    if (lines[at].find("imageStore(") != std::string::npos)
                        found.push_back(genericName(file.lexically_relative(shaders)) + ":" + std::to_string(at + 1));
            }

            EXPECT_TRUE(found.empty()) << "a store no census counts — use RTX_STORE_COUNTED or RTX_STORE_WORDS:\n"
                                       << joined(found);
        }

        /// **Every temporal filter weighs its taps by the one gather** (`RTX_HISTORY_SHARES` in
        /// `surfacematch.glsl`): a kernel that calls `historyShare` itself has written the gather
        /// again, and the five copies the reviews found had drifted apart — one lost its "no history"
        /// test. A kernel still reads its own payload at each tap (`historyTap`).
        TEST(RtxSourceTreeTest, everyHistoryIsWeighedByTheOneGather)
        {
            const std::filesystem::path shaders = sBackend / "shaders";
            const std::filesystem::path library = shaders / "lib" / "surfacematch.glsl";

            std::vector<std::string> found;
            for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(shaders))
            {
                const std::filesystem::path& file = entry.path();
                if (!entry.is_regular_file() || file == library)
                    continue;

                const std::vector<std::string> lines = linesOf(file);
                for (std::size_t at = 0; at < lines.size(); ++at)
                    if (lines[at].find("historyShare(") != std::string::npos)
                        found.push_back(genericName(file.lexically_relative(shaders)) + ":" + std::to_string(at + 1));
            }

            EXPECT_TRUE(found.empty()) << "a history weighed outside RTX_HISTORY_SHARES:\n" << joined(found);
        }

        /// **The census has a word for every module the build compiles**, the probes among them,
        /// since the tests' device counts: one past `CENSUS_KERNELS` stops the device as its first
        /// pipeline is made, which a run that never makes it would not see.
        TEST(RtxSourceTreeTest, theCensusHasAWordForEveryModule)
        {
            constexpr std::array<std::string_view, 7> stages{ ".comp", ".rgen", ".rchit", ".rahit", ".rmiss", ".vert",
                ".frag" };

            std::uint32_t modules = 0;
            for (const std::filesystem::directory_entry& entry :
                std::filesystem::recursive_directory_iterator(sBackend / "shaders"))
            {
                const std::string extension = genericName(entry.path().extension());
                if (std::find(stages.begin(), stages.end(), extension) != stages.end())
                    ++modules;
            }

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

        const std::array<FolderOrder, 3> sFolderOrders{
            FolderOrder{ "components/rtx",
                { "shaders", "common", "image", "preprocess", "scene", "frame", "world", "renderer", "mirror",
                    "environment", "view" } },
            FolderOrder{ "components/rtxvulkan",
                { "shaders", "spirv", "device", "pipeline", "texture", "scene", "trace", "upscale", "display",
                    "present", "gui", "" } },
            FolderOrder{ "apps/rtxtool", { "instruments", "model", "" } },
        };

        /// The folder under an order's root that a path stands in: the first of its names, or
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
            for (const FolderOrder& order : sFolderOrders)
            {
                const std::filesystem::path root = sRoot / order.mRoot;
                const std::string rooted = std::string(order.mRoot) + '/';
                const auto rankOf = [&](const std::string_view folder) -> std::optional<std::size_t> {
                    const auto at = std::find(order.mOrder.begin(), order.mOrder.end(), folder);
                    if (at == order.mOrder.end())
                        return std::nullopt;
                    return static_cast<std::size_t>(at - order.mOrder.begin());
                };

                for (const std::filesystem::directory_entry& entry :
                    std::filesystem::recursive_directory_iterator(root))
                {
                    const std::filesystem::path& file = entry.path();
                    if (file.extension() != ".cpp" && file.extension() != ".hpp")
                        continue;

                    const std::string relative = genericName(file.lexically_relative(sRoot));
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

        /// **A header both languages read stands with the C++ that reads it.** The core's shader
        /// folder holds what core C++ reads, and a header that only the backend and its GLSL read is
        /// the backend's, in `components/rtxvulkan/shaders/shared/`, whatever it states: the GLSL
        /// that uses it lives in the backend already. And nothing of the core reads the backend.
        TEST(RtxSourceTreeTest, aSharedHeaderStandsWithTheCodeThatReadsIt)
        {
            const std::filesystem::path core = sRoot / "components" / "rtx";
            const std::filesystem::path shaders = core / "shaders";

            std::set<std::string, std::less<>> read;
            std::vector<std::string> found;
            for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(core))
            {
                const std::filesystem::path& file = entry.path();
                const std::filesystem::path extension = file.extension();
                if (extension != ".cpp" && extension != ".hpp" && extension != ".h")
                    continue;

                for (const std::string& line : linesOf(file))
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
                    if (named.parent_path() == shaders && named != file.lexically_normal())
                        read.insert(Files::pathToUnicodeString(named.filename()));
                }
            }

            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(shaders))
            {
                const std::string name = Files::pathToUnicodeString(entry.path().filename());
                if (entry.path().extension() == ".h" && !read.contains(name))
                    found.push_back("components/rtx/shaders/" + name
                        + ": no file of the core reads it — it belongs in components/rtxvulkan/shaders/shared/");
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

        /// **Every file under the backend's shaders is named in its CMake file**: an entry shader left
        /// out of `RTX_SHADERS` is never built and fails only when a pass asks for it, and an
        /// include or a shared header left out is one the IDE lists nowhere. Named as `shaders/…`,
        /// which is how every list there spells it.
        TEST(RtxSourceTreeTest, everyShaderFileIsNamedInTheBackendsCMakeFile)
        {
            std::set<std::string, std::less<>> named;
            for (const std::string& line : linesOf(sBackend / "CMakeLists.txt"))
            {
                std::size_t at = 0;
                while ((at = line.find("shaders/", at)) != std::string::npos)
                {
                    const std::size_t end = line.find_first_of(" \t()\"", at);
                    named.insert(line.substr(at, end == std::string::npos ? std::string::npos : end - at));
                    at = end == std::string::npos ? line.size() : end;
                }
            }

            std::vector<std::string> found;
            for (const std::filesystem::directory_entry& entry :
                std::filesystem::recursive_directory_iterator(sBackend / "shaders"))
            {
                if (!entry.is_regular_file())
                    continue;
                const std::string relative = genericName(entry.path().lexically_relative(sBackend));
                if (!named.contains(relative))
                    found.push_back(relative + " is named in no list of components/rtxvulkan/CMakeLists.txt");
            }
            std::ranges::sort(found);

            EXPECT_TRUE(found.empty()) << joined(found);
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
                    return genericName(one).find("/rtx/support/device/") != std::string::npos;
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
                    found.push_back(
                        Files::pathToUnicodeString(file.filename()) + " opens no device: list it in RTX_TEST_FILES");

                const std::vector<std::string> lines = linesOf(file);
                for (std::size_t at = 0; at < lines.size(); ++at)
                {
                    const std::string_view code = codeOf(lines[at]).substr(skipSpace(lines[at], 0));
                    if (code.starts_with("TEST(") || code.starts_with("TEST_P("))
                        found.push_back(Files::pathToUnicodeString(file.filename()) + ':' + std::to_string(at + 1)
                            + ": a test over no device fixture, which CI never runs here");
                }
            }

            for (const std::string_view list : { "RTX_TEST_FILES", "RTX_TEST_SUPPORT" })
                for (const std::filesystem::path& file : listedIn(list))
                    if (reachesDevice(file))
                        found.push_back(Files::pathToUnicodeString(file.filename()) + " reaches the device's support: list it in "
                                                                   "RTX_GPU_TEST_FILES");

            EXPECT_TRUE(found.empty()) << joined(found);
        }
    }
}
