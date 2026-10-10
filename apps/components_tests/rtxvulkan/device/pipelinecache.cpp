#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <volk.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/result.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/owned.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/pipelinecache.hpp>
#include <components/rtxvulkan/device/requirements.hpp>
#include <components/testing/util.hpp>

namespace Rtx
{
    namespace
    {
        struct RtxPipelineCacheTest : Testing::DeviceTest
        {
            /// The blob the driver would save, which every leg below is built from.
            ///
            /// **From a cache with nothing compiled into it, rather than the suite's device's.**
            /// Every leg reads the header and the size and nothing between them, while the suite's
            /// device holds what the renderer compiled — a quarter of a gigabyte here, copied per
            /// leg and four times over inside one of them. And a size the legs cannot afford to
            /// depend on: `sMostBytes` is 256 MiB, so a live cache past that would make the leg
            /// which grows a blob to one byte more write off the end of its own buffer.
            std::vector<std::uint8_t> deviceBlob()
            {
                const VkDevice device = getDevice().getHandle();
                const VkPipelineCacheCreateInfo nothing{ .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .initialDataSize = 0,
                    .pInitialData = nullptr };

                VkPipelineCache made = VK_NULL_HANDLE;
                EXPECT_EQ(vkCreatePipelineCache(device, &nothing, nullptr, &made), VK_SUCCESS);
                const Immediate<VkPipelineCache, vkDestroyPipelineCache> empty(device, made);

                std::size_t bytes = 0;
                EXPECT_EQ(vkGetPipelineCacheData(device, empty.get(), &bytes, nullptr), VK_SUCCESS);

                std::vector<std::uint8_t> blob(bytes);
                EXPECT_EQ(vkGetPipelineCacheData(device, empty.get(), &bytes, blob.data()), VK_SUCCESS);

                return blob;
            }

            const VkPhysicalDeviceProperties& deviceProperties()
            {
                return getDevice().getPhysicalDevice().getProperties().mProperties2.properties;
            }

            /// What the cache directory holds, by name, sorted so a comparison is against a list and
            /// not against whatever order the filesystem answered in.
            static std::vector<std::string> filesIn(const std::filesystem::path& directory)
            {
                std::vector<std::string> names;
                for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory))
                    names.push_back(Files::pathToUnicodeString(entry.path().filename()));

                std::sort(names.begin(), names.end());
                return names;
            }
        };

        /// A cache saved while it stands is on the disk before it goes, and a save with nothing
        /// compiled since writes nothing, then or as it goes.
        ///
        /// **What a crash after the start's compile keeps** (`VulkanRenderer::awaitKernels`): the
        /// file is there while the cache still stands. A rewrite is seen by the file's time, set
        /// back an hour after the first save so that one in the same tick would still show.
        TEST_F(RtxPipelineCacheTest, aSavedCacheIsOnTheDiskAndASecondSaveWritesNothing)
        {
            const std::filesystem::path cacheDirectory = TestingOpenMW::outputFilePath("cache-save-test");
            std::filesystem::remove_all(cacheDirectory);

            std::filesystem::file_time_type stamped;
            {
                PipelineCache cache(getDevice().getHandle(), deviceProperties(),
                    PipelineCacheSpec{ .mDirectory = cacheDirectory }, Testing::getShaderDirectory());
                ASSERT_NE(cache.getHandle(), VK_NULL_HANDLE) << "a cache was made";
                ASSERT_TRUE(filesIn(cacheDirectory).empty()) << "nothing on the disk before a save";

                cache.save();
                const std::vector<std::string> saved = filesIn(cacheDirectory);
                ASSERT_EQ(saved.size(), 1u) << "the save wrote the cache's file while it stands";

                const std::filesystem::path file = cacheDirectory / saved.front();
                const Misc::Result<std::vector<std::uint8_t>, std::string_view> read
                    = PipelineCache::read(file, deviceProperties());
                ASSERT_TRUE(read.isOk()) << "the saved file reads back: " << read.error();

                stamped = std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
                std::filesystem::last_write_time(file, stamped);

                cache.save();
                EXPECT_EQ(std::filesystem::last_write_time(file), stamped) << "a save with nothing new rewrote";
            }

            const std::vector<std::string> left = filesIn(cacheDirectory);
            ASSERT_EQ(left.size(), 1u);
            EXPECT_EQ(std::filesystem::last_write_time(cacheDirectory / left.front()), stamped)
                << "the cache going rewrote what its save had written";
        }

        /// The device has a cache, and what it writes is what the loader will take back.
        ///
        /// **The check the loader applies has to accept the driver's own output**, and there is
        /// nothing about the arithmetic that says so — the header's fields are read at hard-coded
        /// offsets, and one of them off by four would reject every blob ever written. The symptom
        /// would be a cache that silently never hit: no error, no warning, only a test suite slowly
        /// getting slower again and a shader compiled once a process forever.
        ///
        /// So this asks the driver for the blob it would save and runs it through the same door it
        /// would come back in.
        TEST_F(RtxPipelineCacheTest, whatTheDriverWritesIsWhatTheLoaderTakesBack)
        {
            ASSERT_NE(getDevice().getPipelineCache(), VK_NULL_HANDLE) << "the device made a pipeline cache";

            const std::vector<std::uint8_t> blob = deviceBlob();

            // Even a cache nothing was compiled into carries its header, which is all this reads.
            ASSERT_GE(blob.size(), std::size_t{ 32 }) << "a blob is at least a header";

            const Misc::Result<void, std::string_view> accepted = PipelineCache::accepts(blob, deviceProperties());
            EXPECT_TRUE(accepted.isOk()) << "this driver's own blob: " << accepted.error();
        }

        /// And it refuses what another machine wrote, what a dead process left half written, and
        /// what has grown past keeping.
        ///
        /// **The negative leg, because a check that accepted everything would pass the one above.**
        /// The first three are real files that could turn up in a cache directory: a blob from the
        /// other card in a two-card machine, one from before a driver update, and the tail end of a
        /// write that never finished.
        ///
        /// **The offsets below are written out again on purpose.** Sharing the loader's constants
        /// would make this a tautology — an offset wrong in both places agrees with itself and the
        /// test goes green. These are the numbers the specification gives for
        /// `VkPipelineCacheHeaderVersionOne`: the vendor at eight, the UUID at sixteen, and
        /// thirty-two bytes of header in all.
        ///
        /// **The last one is the leg the driver has no opinion about.** A blob this driver wrote is
        /// one it will read back however large it has grown, so that header is the device's own and
        /// the size is the only thing left to refuse it for. What the name does not cover is a file
        /// that is not a cache at all, and one run's own pipelines outgrowing what is worth keeping.
        TEST_F(RtxPipelineCacheTest, aBlobThisRunWillNotSeedFromIsRefused)
        {
            const std::vector<std::uint8_t> blob = deviceBlob();
            const VkPhysicalDeviceProperties& properties = deviceProperties();
            ASSERT_TRUE(PipelineCache::accepts(blob, properties).isOk());

            // A header of another length, in its first word.
            std::vector<std::uint8_t> foreign = blob;
            foreign.at(0) ^= 0xFF;
            EXPECT_EQ(
                PipelineCache::accepts(foreign, properties).error(), "has no header of the one version Vulkan defines");

            // Another vendor's, at byte eight of the header.
            std::vector<std::uint8_t> elsewhere = blob;
            elsewhere.at(8) ^= 0xFF;
            EXPECT_EQ(PipelineCache::accepts(elsewhere, properties).error(), "was written for another vendor's device");

            // The other card of the vendor, at byte twelve.
            std::vector<std::uint8_t> otherCard = blob;
            otherCard.at(12) ^= 0xFF;
            EXPECT_EQ(PipelineCache::accepts(otherCard, properties).error(), "was written for another device");

            // The same driver after an update, which is what the UUID is for.
            std::vector<std::uint8_t> updated = blob;
            updated.at(16) ^= 0xFF;
            EXPECT_EQ(PipelineCache::accepts(updated, properties).error(), "was written by another driver build");

            // And a write that stopped part way through the header itself.
            EXPECT_EQ(
                PipelineCache::accepts(std::span(blob).first(31), properties).error(), "is shorter than a cache header")
                << "a torn header";
            EXPECT_EQ(PipelineCache::accepts({}, properties).error(), "is shorter than a cache header")
                << "nothing at all";

            std::vector<std::uint8_t> grown(PipelineCache::sMostBytes + 1, 0);
            std::copy(blob.begin(), blob.end(), grown.begin());
            EXPECT_EQ(PipelineCache::accepts(grown, properties).error(), "is larger than a cache is kept")
                << "one byte past what is kept";

            grown.resize(PipelineCache::sMostBytes);
            EXPECT_TRUE(PipelineCache::accepts(grown, properties).isOk()) << "exactly what is kept";
        }

        /// **A file is read back only whole, and only by the device and the driver that wrote it**;
        /// and a folder that cannot be read starts the cache empty rather than failing.
        ///
        /// A cache with nothing compiled into it is written by its own destructor, and read back.
        /// Then copies of that file, each wrong in one way the driver's own header cannot show: a
        /// body cut short, a body byte flipped, another driver version, and a file in the format
        /// before this one, which opens with the driver's blob. **The offsets are written out
        /// again**, for the reason the blob test gives: the format's magic at nought, the driver's
        /// version at twenty, and sixty-four bytes of header in all.
        TEST_F(RtxPipelineCacheTest, aFileIsReadBackOnlyWholeAndByItsOwnDriver)
        {
            const std::filesystem::path scratch = TestingOpenMW::outputFilePath("cache-file-test");
            std::filesystem::remove_all(scratch);
            const std::filesystem::path cacheDirectory = scratch / "cache";
            const VkPhysicalDeviceProperties& properties = deviceProperties();
            const auto made = [&] {
                return PipelineCache(getDevice().getHandle(), properties,
                    PipelineCacheSpec{ .mDirectory = cacheDirectory }, Testing::getShaderDirectory());
            };

            {
                const PipelineCache written = made();
            }
            const std::vector<std::string> files = filesIn(cacheDirectory);
            ASSERT_EQ(files.size(), 1u) << "the destructor wrote the cache";
            const std::filesystem::path path = cacheDirectory / files.front();

            const Misc::Result<std::vector<std::uint8_t>, std::string_view> whole
                = PipelineCache::read(path, properties);
            ASSERT_TRUE(whole.isOk()) << whole.error();
            EXPECT_EQ(whole.value(), deviceBlob()) << "the body is the driver's blob as it stood";

            std::vector<char> bytes;
            {
                std::ifstream reading(path, std::ios::binary);
                bytes.assign(std::istreambuf_iterator<char>(reading), std::istreambuf_iterator<char>());
            }
            ASSERT_EQ(bytes.size(), 64 + whole.value().size());

            const auto readAs = [&](const std::vector<char>& file) {
                const std::filesystem::path copy = scratch / "copy.pipelinecache";
                std::ofstream(copy, std::ios::binary | std::ios::trunc)
                    .write(file.data(), static_cast<std::streamsize>(file.size()));
                const Misc::Result<std::vector<std::uint8_t>, std::string_view> read
                    = PipelineCache::read(copy, properties);
                return read.isOk() ? std::string_view("read") : read.error();
            };

            std::vector<char> truncated = bytes;
            truncated.pop_back();
            EXPECT_EQ(readAs(truncated), "holds another length than its header says");

            std::vector<char> flipped = bytes;
            flipped.back() = static_cast<char>(flipped.back() ^ 0x01);
            EXPECT_EQ(readAs(flipped), "holds another body than its header says");

            std::vector<char> updated = bytes;
            updated.at(20) = static_cast<char>(updated.at(20) ^ 0x01);
            EXPECT_EQ(readAs(updated), "was written on another device or driver");

            std::vector<char> old(bytes.begin() + 64, bytes.end());
            old.resize(std::max<std::size_t>(old.size(), 64), 0);
            EXPECT_EQ(readAs(old), "is in no format this renderer writes");

            EXPECT_EQ(readAs(bytes), "read") << "and the copy itself, whole";

            // A folder nobody may read: the sweep and the read both fail, and the cache starts empty.
            std::filesystem::permissions(cacheDirectory, std::filesystem::perms::none);
            EXPECT_NO_THROW({
                const PipelineCache unreadable = made();
                EXPECT_NE(unreadable.getHandle(), VK_NULL_HANDLE);
            });
            std::filesystem::permissions(cacheDirectory, std::filesystem::perms::owner_all);
            std::filesystem::remove_all(scratch);
        }

        /// The name carries the shaders, and every cache that is not this run's is swept.
        ///
        /// **This is the whole of the eviction, and Vulkan supplies none of it.** A blob cannot be
        /// pruned entry by entry, and nothing in the API has an opinion about a cache full of
        /// pipelines for shaders that no longer exist — so what keeps the directory from holding one
        /// file per driver and per edit for ever is that a run can name exactly one file live and
        /// remove the rest.
        ///
        /// **Two shader sets differing in one byte**, because a digest that ignored the contents
        /// would key both the same and the second run would inherit the first's pipelines: no
        /// symptom at all until a changed shader traced with the old one's code.
        ///
        /// **And a file that is not ours survives**, because a cache directory is shared with
        /// whatever else the game keeps there.
        TEST_F(RtxPipelineCacheTest, theNameCarriesTheShadersAndOtherCachesAreKeptToABound)
        {
            const std::filesystem::path scratch = TestingOpenMW::outputFilePath("cache-test");
            std::filesystem::remove_all(scratch);

            const std::filesystem::path cacheDirectory = scratch / "cache";
            const std::filesystem::path edited = scratch / "edited";
            std::filesystem::create_directories(edited);
            std::filesystem::copy(Testing::getShaderDirectory(), edited, std::filesystem::copy_options::recursive);

            const auto only = [&](const std::filesystem::path& shaders) {
                const PipelineCache cache(getDevice().getHandle(), deviceProperties(),
                    PipelineCacheSpec{ .mDirectory = cacheDirectory }, shaders);
                EXPECT_NE(cache.getHandle(), VK_NULL_HANDLE) << "a cache was made";
            };

            only(Testing::getShaderDirectory());

            std::vector<std::string> after = filesIn(cacheDirectory);
            ASSERT_EQ(after.size(), 1u) << "one run leaves one file";
            const std::string first = after.front();
            EXPECT_TRUE(first.starts_with("rtx-")) << first;

            // A cache from a driver this machine no longer runs, and something that is not ours.
            std::ofstream(cacheDirectory / "rtx-some-other-driver.pipelinecache") << "stale";
            std::ofstream(cacheDirectory / "keep-me.txt") << "not a pipeline cache";

            // One byte of one module, which is what a shader edit comes to.
            const std::filesystem::path module = edited / "tone.comp.spv";
            std::vector<char> bytes;
            {
                std::ifstream reading(module, std::ios::binary);
                bytes.assign(std::istreambuf_iterator<char>(reading), std::istreambuf_iterator<char>());
            }
            ASSERT_FALSE(bytes.empty()) << "there is a module to edit";
            bytes.back() = static_cast<char>(bytes.back() ^ 0xFF);
            std::ofstream(module, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));

            only(edited);

            after = filesIn(cacheDirectory);

            // **The other caches stay.** A name carries the card, the driver and the shaders it was
            // compiled for, so a cache that is not this run's is another thing somebody runs — a
            // second card, the shader tree before the edit — and deleting it made every switch a
            // cold compile.
            EXPECT_EQ(after.size(), 4u) << "the two runs' caches, the stale driver's, and the file that is not ours";
            EXPECT_NE(std::find(after.begin(), after.end(), first), after.end())
                << "the cache of the shaders before the edit was swept";
            EXPECT_NE(std::find(after.begin(), after.end(), "rtx-some-other-driver.pipelinecache"), after.end())
                << "another driver's cache was swept";
            EXPECT_NE(std::find(after.begin(), after.end(), "keep-me.txt"), after.end())
                << "a file this renderer did not write is left alone";

            // **Bounded, because a driver arrives every few weeks.** Enough others to pass the
            // bound, aged so the order is the file system's to state rather than the test's.
            for (int at = 0; at < 6; ++at)
            {
                const std::filesystem::path old
                    = cacheDirectory / ("rtx-older-" + std::to_string(at) + ".pipelinecache");
                std::ofstream(old) << "stale";
                std::filesystem::last_write_time(
                    old, std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * (10 - at)));
            }

            // A partial write a process was killed in the middle of, older than every cache.
            const std::filesystem::path crashed = cacheDirectory / "rtx-crashed.1234.partial.pipelinecache";
            std::ofstream(crashed) << "half";
            std::filesystem::last_write_time(
                crashed, std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * 30));

            only(Testing::getShaderDirectory());

            after = filesIn(cacheDirectory);
            EXPECT_EQ(after.size(), 6u) << "this run's cache, the four kept beside it, and the file that is not ours";
            EXPECT_EQ(
                std::find(after.begin(), after.end(), Files::pathToUnicodeString(crashed.filename())), after.end())
                << "a partial write was left for good";
            EXPECT_NE(std::find(after.begin(), after.end(), "keep-me.txt"), after.end())
                << "a file this renderer did not write is left alone";
            EXPECT_EQ(std::find(after.begin(), after.end(), "rtx-older-0.pipelinecache"), after.end())
                << "the oldest cache was kept";

            // **And no directory is no cache object**, `PipelineCacheSpec::mDirectory`: a
            // measuring process shares nothing between its compiles, not even an empty object the
            // driver would fill as they run.
            const PipelineCache none(
                getDevice().getHandle(), deviceProperties(), PipelineCacheSpec{}, Testing::getShaderDirectory());
            EXPECT_EQ(none.getHandle(), VK_NULL_HANDLE) << "a spec with no directory made a cache object";
            EXPECT_EQ(filesIn(cacheDirectory).size(), 6u) << "a spec with no directory touched the directory";

            std::filesystem::remove_all(scratch);
        }
    }
}
