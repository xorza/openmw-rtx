#include "guiinstance.hpp"

#include <filesystem>
#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>

#include <MyGUI_FactoryManager.h>
#include <MyGUI_Gui.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_ResourceManager.h>

#include <components/fontloader/fontloader.hpp>
#include <components/myguiplatform/myguiplatform.hpp>
#include <components/myguiplatform/scalinglayer.hpp>
#include <components/myguirtx/rendermanager.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/archive.hpp>
#include <components/vfs/file.hpp>
#include <components/vfs/filemap.hpp>
#include <components/vfs/filesystemarchive.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/widgets/widgets.hpp>

namespace Rtx::Testing
{
    namespace
    {
        /// A file of `files/data` that counts each time it is opened.
        class CountedFile final : public VFS::File
        {
        public:
            CountedFile(VFS::File& file, std::uint32_t& reads)
                : mFile(file)
                , mReads(reads)
            {
            }

            Files::IStreamPtr open() override
            {
                ++mReads;
                return mFile.open();
            }

            std::filesystem::file_time_type getLastModified() const override { return mFile.getLastModified(); }
            std::string getStem() const override { return mFile.getStem(); }

        private:
            VFS::File& mFile;
            std::uint32_t& mReads;
        };

        /// `files/data`, each of its files counted where it is opened, and a test's own files over it,
        /// which are not.
        class CountedArchive final : public VFS::Archive
        {
        public:
            CountedArchive(const std::filesystem::path& path, VFS::FileMap overrides, std::uint32_t& reads)
                : mArchive(path)
                , mOverrides(std::move(overrides))
                , mReads(reads)
            {
            }

            void listResources(VFS::FileMap& out) override
            {
                VFS::FileMap listed;
                mArchive.listResources(listed);
                for (const auto& [path, file] : listed)
                    out[path] = &mFiles.try_emplace(path, *file, mReads).first->second;
                for (const auto& [path, file] : mOverrides)
                    out[path] = file;
            }

            bool contains(VFS::Path::NormalizedView file) const override
            {
                return mOverrides.contains(file) || mArchive.contains(file);
            }
            std::string getDescription() const override { return mArchive.getDescription(); }

        private:
            VFS::FileSystemArchive mArchive;
            VFS::FileMap mOverrides;
            std::uint32_t& mReads;
            std::map<VFS::Path::Normalized, CountedFile, std::less<>> mFiles;
        };
    }

    GuiInstance::GuiInstance(GuiSetup setup)
        : mImages(&mVfs, 0.0)
    {
        mVfs.addArchive(
            std::make_unique<CountedArchive>(std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "data",
                std::move(setup.mOverrides), mVfsReads));
        mVfs.buildIndex();

        mRenderer.mExtents = Rtx::FrameExtents{ .mRenderWidth = setup.mWidth,
            .mRenderHeight = setup.mHeight,
            .mOutputWidth = setup.mWidth,
            .mOutputHeight = setup.mHeight };

        auto manager = std::make_unique<MyGUIRtx::RenderManager>(mRenderer, &mImages, setup.mScale);
        mRenderManager = manager.get();
        constexpr VFS::Path::NormalizedView resources("mygui");
        mPlatform = std::make_unique<MyGUIPlatform::Platform>(std::move(manager), &mVfs, resources, "");

        mGui = std::make_unique<MyGUI::Gui>();
        mGui->initialise({});

        MyGUI::LanguageManager::getInstance().eventRequestTag = MyGUI::newDelegate(this, &GuiInstance::onRetrieveTag);

        mFonts = std::make_unique<Gui::FontLoader>(ToUTF8::WINDOWS_1252, &mVfs, *mPlatform->getDataManagerPtr(),
            Gui::FontFit{ .mRaster = setup.mScale, .mView = MyGUI::RenderManager::getInstance().getViewSize() },
            false);

        mPlatform->getRenderManagerPtr()->registerFactories();
        MyGUI::FactoryManager::getInstance().registerFactory<MyGUIPlatform::ScalingLayer>("Layer");
        Gui::registerAllWidgets();

        // The default font's name, the layers and the text skins: what a text needs, and none of
        // the game's own resource types, which this binary does not have.
        for (const char* const file : { "openmw_settings.xml", "openmw_layers.xml", "openmw_text.skin.xml" })
            MyGUI::ResourceManager::getInstance().load(file);
    }

    GuiInstance::~GuiInstance()
    {
        MyGUI::LanguageManager::getInstance().eventRequestTag.clear();
        mFonts.reset();
        mGui->shutdown();
        mPlatform->shutdown();
    }

    DrawnGui GuiInstance::draw()
    {
        mRenderManager->collectDrawCalls();
        return DrawnGui{ .mVertices = mRenderer.mGuiVertices, .mBatches = mRenderer.mGuiBatches };
    }

    void GuiInstance::onRetrieveTag(const MyGUI::UString& /*tag*/, MyGUI::UString& result)
    {
        result = "1 1 1";
    }
}
