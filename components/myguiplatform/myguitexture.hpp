#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include <MyGUI_ITexture.h>
#include <osg/ref_ptr>

namespace osg
{
    class Image;
    class Texture2D;
    class StateSet;
}

namespace Resource
{
    class ImageManager;
}

namespace MyGUIPlatform
{

    class OSGTexture final : public MyGUI::ITexture
    {
        std::string mName;
        Resource::ImageManager* mImageManager;

        osg::ref_ptr<osg::Image> mLockedImage;
        osg::ref_ptr<osg::Texture2D> mTexture;
        osg::ref_ptr<osg::StateSet> mInjectState;
        MyGUI::PixelFormat mFormat;
        MyGUI::TextureUsage mUsage;
        std::size_t mNumElemBytes;

        std::int32_t mWidth;
        std::int32_t mHeight;

    public:
        OSGTexture(std::string name, Resource::ImageManager* imageManager);
        explicit OSGTexture(osg::Texture2D* texture, osg::StateSet* injectState = nullptr);
        ~OSGTexture() override;
        OSGTexture(const OSGTexture&) = default;
        OSGTexture& operator=(const OSGTexture&) = default;

        osg::StateSet* getInjectState() { return mInjectState; }

        const std::string& getName() const override { return mName; }

        void createManual(
            std::int32_t width, std::int32_t height, MyGUI::TextureUsage usage, MyGUI::PixelFormat format) override;
        void loadFromFile(const std::string& fname) override;
        void saveToFile(const std::string& fname) override;

        void destroy() override;

        void* lock(MyGUI::TextureUsage access) override;
        void unlock() override;
        bool isLocked() const override { return mLockedImage.valid(); }

        std::int32_t getWidth() const override { return mWidth; }
        std::int32_t getHeight() const override { return mHeight; }

        MyGUI::PixelFormat getFormat() const override { return mFormat; }
        MyGUI::TextureUsage getUsage() const override { return mUsage; }
        std::size_t getNumElemBytes() const override { return mNumElemBytes; }

        MyGUI::IRenderTarget* getRenderTarget() override;

        void setShader(const std::string& shaderName) override;

        /*internal:*/
        osg::Texture2D* getTexture() const { return mTexture.get(); }
    };

    // Rows stay in the decoder's bottom-up order.
    std::unique_ptr<MyGUI::ITexture> createTextureFromJpeg(std::span<const char> data);

}
