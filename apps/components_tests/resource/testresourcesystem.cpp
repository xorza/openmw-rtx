#include <array>
#include <atomic>
#include <cstdint>
#include <latch>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Array>
#include <osg/Drawable>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Image>
#include <osg/NodeVisitor>
#include <osg/PrimitiveSet>
#include <osg/Texture2D>

#include <apps/components_tests/rtx/support/ddsfile.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/resource/cachebudget.hpp>
#include <components/resource/imageheader.hpp>
#include <components/resource/imagemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/resource/sptfilemanager.hpp>
#include <components/sceneutil/material.hpp>
#include <components/sceneutil/riggeometry.hpp>
#include <components/sceneutil/shadow.hpp>
#include <components/spt/file.hpp>
#include <components/testing/util.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>

namespace
{
    using namespace testing;

    TEST(ResourceResourceSystem, scenemanager_getinstance_should_be_thread_safe)
    {
        const VFS::Manager vfsManager;
        const ToUTF8::Utf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resourceSystem(&vfsManager, 1.0, &encoder.getStatelessEncoder());
        Resource::SceneManager* sceneManager = resourceSystem.getSceneManager();
        const Files::ConfigurationManager configurationManager;
        sceneManager->setShaderPath(configurationManager.getLocalPath() / "resources/shaders");

        auto defines = Shader::getDefaultDefines();

        auto shadowDefines = SceneUtil::ShadowManager::getShadowsDisabledDefines();

        osg::ref_ptr<SceneUtil::LightManager> lightManager
            = new SceneUtil::LightManager(SceneUtil::LightSettings{}, &resourceSystem);
        auto lightDefines = lightManager->getLightDefines();

        for (const auto& define : shadowDefines)
            defines[define.first] = define.second;
        for (const auto& define : lightDefines)
            defines[define.first] = define.second;

        sceneManager->getShaderManager().setGlobalDefines(defines);

        constexpr VFS::Path::NormalizedView noSuchPath("meshes/whatever.nif");
        std::vector<std::thread> threads;

        threads.reserve(50);
        for (std::int32_t i = 0; i < 50; ++i)
        {
            threads.emplace_back([=]() { sceneManager->getInstance(noSuchPath); });
        }
        for (std::thread& thread : threads)
            thread.join();
    }

    /// **A path let go of is loaded again**: a template asked twice is the one the cache kept, and asked
    /// after `letGo` names its path, a new one, while one another path names stays.
    TEST(ResourceResourceSystem, scenemanager_letgo_drops_the_paths_it_names)
    {
        const VFS::Manager vfsManager;
        const ToUTF8::Utf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resourceSystem(&vfsManager, 1.0, &encoder.getStatelessEncoder());
        Resource::SceneManager* sceneManager = resourceSystem.getSceneManager();
        sceneManager->setModelPrograms(Resource::ModelPrograms::Omitted);

        constexpr VFS::Path::NormalizedView first("meshes/first.nif");
        constexpr VFS::Path::NormalizedView kept("meshes/kept.nif");
        const osg::ref_ptr<const osg::Node> loaded = sceneManager->getTemplate(first);
        const osg::ref_ptr<const osg::Node> keptLoaded = sceneManager->getTemplate(kept);
        EXPECT_EQ(sceneManager->getTemplate(first), loaded) << "the cache's own";

        const std::vector<std::string> letGo{ std::string(first.value()) };
        sceneManager->letGo(letGo);
        EXPECT_NE(sceneManager->getTemplate(first), loaded) << "loaded again";
        EXPECT_EQ(sceneManager->getTemplate(kept), keptLoaded) << "a path not named stays";
    }

    /// **An image is kept by its pixels' bytes**: a 2 by 2 TGA of 32-bit pixels holds 2 * 2 * 4 = 16, which its
    /// cache and the budget count.
    TEST(ResourceResourceSystem, an_image_is_kept_by_its_pixels_bytes)
    {
        // 18 bytes of header (type 2, uncompressed true colour; 2 by 2; 32 bits, 8 of them alpha), then the pixels.
        std::string tga(18, '\0');
        tga[2] = 2;
        tga[12] = 2;
        tga[14] = 2;
        tga[16] = 32;
        tga[17] = 8;
        tga.append(16, '\x7f');
        TestingOpenMW::VFSTestFile file(tga);
        constexpr VFS::Path::NormalizedView path("textures/two.tga");
        const std::unique_ptr<VFS::Manager> vfs = TestingOpenMW::createTestVFS({ { path, &file } });
        const ToUTF8::Utf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resources(vfs.get(), 1.0, &encoder.getStatelessEncoder());

        const osg::ref_ptr<osg::Image> image = resources.getImageManager()->getImage(path);
        ASSERT_NE(image.get(), resources.getImageManager()->getWarningImage());
        EXPECT_EQ(image->getTotalSizeInBytesIncludingMipmaps(), 16u);
        EXPECT_EQ(resources.getCacheBudget().getHeld(), 16u);
    }

    /// **A model's image is its header where the renderer reads texels itself**: the file's size, format, levels and
    /// name, no texel, one object however often a model names the file, and read once; its texels its manager's,
    /// from the cache while it holds them and from the file once let go of, a reload. The image itself where models
    /// keep their texels, and the warning image for a file there is none of.
    TEST(ResourceResourceSystem, a_models_image_is_its_header_whose_texels_its_manager_reads)
    {
        // 8 by 8 in DXT1, three levels: 4 blocks, 1 and 1, 8 bytes each, 48.
        TestingOpenMW::VFSTestFile file(Rtx::Testing::ddsDxt1(8, 8, 3, 0x55));
        constexpr VFS::Path::NormalizedView path("textures/eight.dds");
        const std::unique_ptr<VFS::Manager> vfs = TestingOpenMW::createTestVFS({ { path, &file } });
        const ToUTF8::Utf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resources(vfs.get(), 1.0, &encoder.getStatelessEncoder());
        Resource::ImageManager& images = *resources.getImageManager();

        const osg::ref_ptr<osg::Image> texels = images.getModelImage(path);
        EXPECT_FALSE(Resource::isHeader(*texels)) << "texels, where models keep them";
        EXPECT_EQ(texels->getTotalSizeInBytesIncludingMipmaps(), 48u);

        images.setModelImages(Resource::ModelImages::Headers, [](const osg::Image&, const Resource::ImageHeader&) {});
        const osg::ref_ptr<osg::Image> header = images.getModelImage(path);
        ASSERT_TRUE(Resource::isHeader(*header));
        EXPECT_EQ(header->data(), nullptr);
        EXPECT_EQ(header->s(), 8);
        EXPECT_EQ(header->t(), 8);
        EXPECT_EQ(header->getPixelFormat(), texels->getPixelFormat());
        EXPECT_EQ(header->getInternalTextureFormat(), texels->getInternalTextureFormat());
        EXPECT_EQ(header->getNumMipmapLevels(), 3u);
        EXPECT_EQ(header->getFileName(), path.value());
        EXPECT_EQ(images.getModelImage(path), header) << "one header a file";

        EXPECT_EQ(Resource::texelsOf(*header), texels) << "the texels the cache holds";
        EXPECT_EQ(Resource::texelsOf(*texels), texels) << "an image that holds its texels is its own";
        EXPECT_EQ(resources.getCacheBudget().getHeld(), 48u);

        // Let go of: the cache drops them, what holds them keeps them, and a model's next ask reads nothing.
        Resource::releaseTexels(*header);
        EXPECT_EQ(resources.getCacheBudget().getHeld(), 0u);
        EXPECT_EQ(texels->getTotalSizeInBytesIncludingMipmaps(), 48u);
        EXPECT_EQ(images.getModelImage(path), header);
        EXPECT_EQ(resources.getCacheBudget().getHeld(), 0u) << "the header asked again, and no texel read";
        const osg::ref_ptr<const osg::Image> again = Resource::texelsOf(*header);
        EXPECT_NE(again, texels) << "read again from the file";
        EXPECT_EQ(again->getTotalSizeInBytesIncludingMipmaps(), 48u);
        EXPECT_EQ(resources.getCacheBudget().getHeld(), 48u);

        EXPECT_EQ(images.getModelImage(VFS::Path::NormalizedView("textures/none.dds")), images.getWarningImage());
    }

    /// **A file eight loaders meet at once is derived once** (`ImageManager::getModelImage`): each takes the one
    /// header, and the deriver ran for one of them, the others waiting on its load or finding its header filed.
    TEST(ResourceResourceSystem, a_file_loaders_meet_at_once_is_derived_once)
    {
        TestingOpenMW::VFSTestFile file(Rtx::Testing::ddsDxt1(8, 8, 3, 0x55));
        constexpr VFS::Path::NormalizedView path("textures/eight.dds");
        const std::unique_ptr<VFS::Manager> vfs = TestingOpenMW::createTestVFS({ { path, &file } });
        const ToUTF8::Utf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resources(vfs.get(), 1.0, &encoder.getStatelessEncoder());
        Resource::ImageManager& images = *resources.getImageManager();
        std::atomic<std::uint32_t> derived = 0;
        images.setModelImages(Resource::ModelImages::Headers,
            [&](const osg::Image&, const Resource::ImageHeader&) { derived.fetch_add(1); });

        constexpr std::size_t sLoaders = 8;
        std::latch start(sLoaders);
        std::array<osg::ref_ptr<osg::Image>, sLoaders> headers;
        {
            std::vector<std::jthread> loaders;
            loaders.reserve(sLoaders);
            for (std::size_t at = 0; at < sLoaders; ++at)
                loaders.emplace_back([&, at] {
                    start.arrive_and_wait();
                    headers[at] = images.getModelImage(path);
                });
        }
        EXPECT_EQ(derived.load(), 1u) << "a file derived by more than one loader";
        for (const osg::ref_ptr<osg::Image>& header : headers)
            EXPECT_EQ(header, headers[0]) << "two headers of one file";
        ASSERT_NE(headers[0], nullptr);
        EXPECT_TRUE(Resource::isHeader(*headers[0]));
    }

    /// **A template is kept by its geometry's bytes and not its images'**, which the image cache counts once
    /// however many templates share one: three vertices (36 bytes), their normals (36) and one set of coordinates
    /// (24), three 16-bit indices (6), and a range of indices (none) come to 102; a skinned drawable adds its
    /// source's four vertices (48), 150; the 16 bytes of its texture's image add nothing.
    TEST(ResourceResourceSystem, a_template_is_kept_by_its_geometry_alone)
    {
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(2, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE);
        ASSERT_EQ(image->getTotalSizeInBytesIncludingMipmaps(), 16u);

        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        geometry->setVertexArray(new osg::Vec3Array(3));
        geometry->setNormalArray(new osg::Vec3Array(3), osg::Array::BIND_PER_VERTEX);
        geometry->setTexCoordArray(0, new osg::Vec2Array(3));
        osg::ref_ptr<osg::DrawElementsUShort> triangle = new osg::DrawElementsUShort(GL_TRIANGLES);
        for (const std::uint16_t index : std::array<std::uint16_t, 3>{ 0, 1, 2 })
            triangle->push_back(index);
        geometry->addPrimitiveSet(triangle);
        geometry->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, 3));
        geometry->getOrCreateStateSet()->setTextureAttributeAndModes(0, new osg::Texture2D(image));

        osg::ref_ptr<osg::Group> root = new osg::Group;
        root->addChild(geometry);
        EXPECT_EQ(Resource::geometryBytes(*root), 102u);

        osg::ref_ptr<osg::Geometry> source = new osg::Geometry;
        source->setVertexArray(new osg::Vec3Array(4));
        osg::ref_ptr<SceneUtil::RigGeometry> skinned = new SceneUtil::RigGeometry;
        skinned->setSourceGeometry(source);
        root->addChild(skinned);
        EXPECT_EQ(Resource::geometryBytes(*root), 150u);
    }

    /// A SpeedTree file of a header and nothing after it, which `Spt::parse` reads: its opening section,
    /// the version it takes, and the section that ends the header, each little-endian.
    std::string headerOnlySpeedTree()
    {
        std::string bytes;
        const auto append = [&bytes](std::uint32_t value) {
            for (std::int32_t shift = 0; shift < 32; shift += 8)
                bytes.push_back(static_cast<char>((value >> shift) & 0xffu));
        };
        constexpr std::string_view version = "__IdvSpt_02_";
        append(1000);
        append(static_cast<std::uint32_t>(version.size()));
        bytes += version;
        append(1001);
        return bytes;
    }

    /// **A SpeedTree file let go of is read again**: `letGo` drops the parsed `.spt` its path names, as it
    /// drops a NIF file, so the preparation's memory does not keep every tree it read, while one another
    /// path names stays.
    TEST(ResourceResourceSystem, scenemanager_letgo_drops_the_speedtree_files_it_names)
    {
        TestingOpenMW::VFSTestFile firstFile(headerOnlySpeedTree());
        TestingOpenMW::VFSTestFile keptFile(headerOnlySpeedTree());
        constexpr VFS::Path::NormalizedView first("trees/first.spt");
        constexpr VFS::Path::NormalizedView kept("trees/kept.spt");
        const std::unique_ptr<VFS::Manager> vfs
            = TestingOpenMW::createTestVFS({ { first, &firstFile }, { kept, &keptFile } });
        const ToUTF8::Utf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resourceSystem(vfs.get(), 1.0, &encoder.getStatelessEncoder());
        Resource::SptFileManager& trees = *resourceSystem.getSptFileManager();

        const Spt::SPTFilePtr read = trees.get(first);
        const Spt::SPTFilePtr keptRead = trees.get(kept);
        EXPECT_EQ(read->mHeader.mVersionId, "__IdvSpt_02_");
        EXPECT_EQ(trees.get(first), read) << "the cache's own";

        const std::vector<std::string> letGo{ std::string(first.value()) };
        resourceSystem.getSceneManager()->letGo(letGo);
        EXPECT_NE(trees.get(first), read) << "read again";
        EXPECT_EQ(trees.get(kept), keptRead) << "a path not named stays";
    }

    /// Every `SceneUtil::Material` a scene's state sets hold, by its name.
    class MaterialsByName : public osg::NodeVisitor
    {
    public:
        MaterialsByName()
            : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
        {
        }

        void apply(osg::Node& node) override
        {
            take(node.getStateSet());
            traverse(node);
        }

        void apply(osg::Drawable& drawable) override { take(drawable.getStateSet()); }

        std::map<std::string, osg::ref_ptr<const SceneUtil::Material>> mFound;

    private:
        void take(const osg::StateSet* stateSet)
        {
            if (stateSet == nullptr)
                return;
            if (const auto* material
                = dynamic_cast<const SceneUtil::Material*>(stateSet->getAttribute(osg::StateAttribute::MATERIAL)))
                mFound[material->getName()] = material;
        }
    };

    /// **The error marker keeps its material**: it names `SceneUtil::Material`, whose `.osgt` wrapper the scene
    /// manager registers as it is made, so a model loaded where there is none is the marker `ErrorMarker::sValue`
    /// states, its material's every value as written there.
    TEST(ResourceResourceSystem, scenemanager_error_marker_keeps_its_material)
    {
        const VFS::Manager vfsManager;
        const ToUTF8::Utf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resourceSystem(&vfsManager, 1.0, &encoder.getStatelessEncoder());
        Resource::SceneManager* sceneManager = resourceSystem.getSceneManager();
        sceneManager->setModelPrograms(Resource::ModelPrograms::Omitted);

        constexpr VFS::Path::NormalizedView missing("meshes/missing.nif");
        const osg::ref_ptr<const osg::Node> marker = sceneManager->getTemplate(missing);
        ASSERT_NE(marker, nullptr);
        MaterialsByName materials;
        // `accept` takes a node a visitor may change, and this one only reads.
        const_cast<osg::Node&>(*marker).accept(materials);

        ASSERT_EQ(materials.mFound.count("Error"), 1u) << "the marker lost its material";
        const SceneUtil::Material& error = *materials.mFound.at("Error");
        EXPECT_EQ(error.getAmbient(), osg::Vec4f(1.0f, 1.0f, 1.0f, 0.5f));
        EXPECT_EQ(error.getDiffuse(), osg::Vec4f(0.8f, 0.704f, 0.32f, 0.5f));
        EXPECT_EQ(error.getSpecular(), osg::Vec4f(0.5f, 0.5f, 0.5f, 0.5f));
        EXPECT_EQ(error.getEmission(), osg::Vec4f(1.0f, 0.88f, 0.4f, 0.5f));
        EXPECT_EQ(error.getShininess(), 28.8f);
    }
}
