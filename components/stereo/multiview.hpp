#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <osg/Camera>
#include <osg/FrameBufferObject>
#include <osg/GL>
#include <osg/ref_ptr>

namespace osg
{
    class FrameBufferObject;
    class Texture;
    class Texture2D;
    class Texture2DArray;
}

namespace osgUtil
{
    class RenderStage;
}

namespace Stereo
{
    class UpdateRenderStagesCallback;

    //! Check if TextureView is supported. Results are undefined if called before configureExtensions().
    bool getTextureViewSupported();

    //! Check if Multiview should be used. Results are undefined if called before configureExtensions().
    bool getMultiview();

    //! Use the provided context to check what extensions are supported and configure use of multiview based on
    //! extensions and settings.
    void configureExtensions(std::uint32_t contextID, bool enableMultiview);

    //! Sets the appropriate vertex buffer hint on OSG's display settings if needed
    void setVertexBufferHint(bool enableMultiview, bool allowDisplayListsForMultiview);

    //! Creates a Texture2D as a texture view into a Texture2DArray
    osg::ref_ptr<osg::Texture2D> createTextureView_Texture2DFromTexture2DArray(
        osg::Texture2DArray* textureArray, std::int32_t layer);

    //! Class that manages the specifics of GL_OVR_Multiview aware framebuffers, separating the layers into separate
    //! framebuffers, and disabling
    class MultiviewFramebuffer
    {
    public:
        MultiviewFramebuffer(std::int32_t width, std::int32_t height, std::int32_t samples);
        ~MultiviewFramebuffer();

        void attachColorComponent(GLint sourceFormat, GLint sourceType, GLint internalFormat);
        void attachDepthComponent(GLint sourceFormat, GLint sourceType, GLint internalFormat);

        osg::FrameBufferObject* multiviewFbo();
        osg::FrameBufferObject* layerFbo(std::int32_t i);
        osg::FrameBufferObject* layerMsaaFbo(std::int32_t i);
        osg::Texture2DArray* multiviewColorBuffer();
        osg::Texture2DArray* multiviewDepthBuffer();
        osg::Texture2D* layerColorBuffer(std::int32_t i);
        osg::Texture2D* layerDepthBuffer(std::int32_t i);

        void attachTo(osg::Camera* camera);
        void detachFrom(osg::Camera* camera);

        std::int32_t width() const { return mWidth; }
        std::int32_t height() const { return mHeight; }
        std::int32_t samples() const { return mSamples; }

    private:
        osg::Texture2D* createTexture(GLint sourceFormat, GLint sourceType, GLint internalFormat);
        osg::Texture2DArray* createTextureArray(GLint sourceFormat, GLint sourceType, GLint internalFormat);

        std::int32_t mWidth;
        std::int32_t mHeight;
        std::int32_t mSamples;
        bool mMultiview;
        osg::ref_ptr<UpdateRenderStagesCallback> mCullCallback;
        osg::ref_ptr<osg::FrameBufferObject> mMultiviewFbo;
        std::array<osg::ref_ptr<osg::FrameBufferObject>, 2> mLayerFbo;
        std::array<osg::ref_ptr<osg::FrameBufferObject>, 2> mLayerMsaaFbo;
        osg::ref_ptr<osg::Texture2DArray> mMultiviewColorTexture;
        osg::ref_ptr<osg::Texture2DArray> mMultiviewDepthTexture;
        std::array<osg::ref_ptr<osg::Texture2D>, 2> mColorTexture;
        std::array<osg::ref_ptr<osg::Texture2D>, 2> mDepthTexture;
    };

    //! Sets up a draw callback on the render stage that performs the MSAA resolve operation
    void setMultiviewMSAAResolveCallback(osgUtil::RenderStage* renderStage);

    //! Sets up or updates multiview matrices for the given stateset
    void setMultiviewMatrices(
        osg::StateSet* stateset, const std::array<osg::Matrix, 2>& projection, bool createInverseMatrices = false);

    //! Sets the width/height of a texture by first down-casting it to the appropriate type. Sets depth to 2 always for
    //! Texture2DArray and Texture2DMultisampleArray.
    void setMultiviewCompatibleTextureSize(osg::Texture* tex, std::int32_t w, std::int32_t h);

    //! Creates a texture (Texture2D, Texture2DMultisample, Texture2DArray, or Texture2DMultisampleArray) based on
    //! multiview settings and sample count.
    osg::ref_ptr<osg::Texture> createMultiviewCompatibleTexture(
        std::int32_t width, std::int32_t height, std::int32_t samples);

    //! Returns a framebuffer attachment from the texture, returning a multiview attachment if the texture is one of
    //! Texture2DArray or Texture2DMultisampleArray
    osg::FrameBufferAttachment createMultiviewCompatibleAttachment(osg::Texture* tex);

    //! If OSG has multiview, returns the magic number used to tell OSG to create a multiview attachment. Otherwise
    //! returns 0.
    std::uint32_t osgFaceControlledByMultiviewShader();

    //! Implements resolving a multisamples multiview framebuffer. Does not automatically reflect changes to the fbo
    //! attachments, must call dirty() when the fbo attachments change.
    class MultiviewFramebufferResolve
    {
    public:
        MultiviewFramebufferResolve(
            osg::FrameBufferObject* msaaFbo, osg::FrameBufferObject* resolveFbo, GLbitfield blitMask);

        void resolveImplementation(osg::State& state);

        void dirty() { mDirtyLayers = true; }

        const osg::FrameBufferObject* resolveFbo() const { return mResolveFbo; }
        const osg::FrameBufferObject* msaaFbo() const { return mMsaaFbo; }

        void setResolveFbo(osg::FrameBufferObject* resolveFbo);
        void setMsaaFbo(osg::FrameBufferObject* msaaFbo);

    private:
        void setupLayers();

        osg::ref_ptr<osg::FrameBufferObject> mResolveFbo;
        std::array<osg::ref_ptr<osg::FrameBufferObject>, 2> mResolveLayers{};
        osg::ref_ptr<osg::FrameBufferObject> mMsaaFbo;
        std::array<osg::ref_ptr<osg::FrameBufferObject>, 2> mMsaaLayers{};

        GLbitfield mBlitMask;
        bool mDirtyLayers = true;
        std::int32_t mWidth = -1;
        std::int32_t mHeight = -1;
    };

    //! Wrapper for osg::CullSettings::InitialFrustumCallback, to avoid exposing osg multiview interfaces outside of
    //! multiview.cpp
    struct InitialFrustumCallback
    {
        explicit InitialFrustumCallback(osg::Camera* camera);
        virtual ~InitialFrustumCallback();
        InitialFrustumCallback(const InitialFrustumCallback&) = delete;
        InitialFrustumCallback& operator=(const InitialFrustumCallback&) = delete;

        virtual void setInitialFrustum(
            osg::CullStack& cullStack, osg::BoundingBoxd& bb, bool& nearCulling, bool& farCulling) const = 0;

        osg::observer_ptr<osg::Camera> mCamera;
    };
}
