#include "multiview.hpp"

#include <osg/FrameBufferObject>
#include <osg/GLExtensions>
#include <osg/Texture2D>
#include <osg/Texture2DArray>
#include <osg/Texture2DMultisample>
#include <osgUtil/CullVisitor>
#include <osgUtil/RenderStage>

// After OpenSceneGraph's headers, which include the system's <GL/gl.h> that Khronos's extension header builds on:
// Windows' names stop at GL 1.1.
#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <GL/glext.h>

#include <components/debug/debuglog.hpp>
#include <components/sceneutil/nodecallback.hpp>

#include "stereomanager.hpp"

namespace Stereo
{
    namespace
    {
        bool getMultiviewSupportedImpl(std::uint32_t contextID)
        {
            Log(Debug::Verbose) << "Disabling Multiview (OSG does not support multiview)";
            return false;
        }

        bool getMultiviewSupported(std::uint32_t contextID)
        {
            static bool supported = getMultiviewSupportedImpl(contextID);
            return supported;
        }

        bool getTextureViewSupportedImpl(std::uint32_t contextID)
        {
            if (!osg::isGLExtensionOrVersionSupported(contextID, "ARB_texture_view", 4.3f))
            {
                Log(Debug::Verbose) << "Disabling texture views (opengl extension \"ARB_texture_view\" not supported)";
                return false;
            }
            return true;
        }

        bool getTextureViewSupported(std::uint32_t contextID)
        {
            static bool supported = getTextureViewSupportedImpl(contextID);
            return supported;
        }

        bool getMultiviewImpl(std::uint32_t contextID)
        {
            if (!Stereo::getStereo())
            {
                Log(Debug::Verbose) << "Disabling Multiview (disabled by config)";
                return false;
            }

            if (!getMultiviewSupported(contextID))
            {
                return false;
            }

            if (!getTextureViewSupported(contextID))
            {
                Log(Debug::Verbose) << "Disabling Multiview (texture views not supported)";
                return false;
            }

            Log(Debug::Verbose) << "Enabling Multiview";
            return true;
        }

        bool getMultiview(std::uint32_t contextID)
        {
            static bool multiView = getMultiviewImpl(contextID);
            return multiView;
        }
    }

    bool getTextureViewSupported()
    {
        return getTextureViewSupported(0);
    }

    bool getMultiview()
    {
        return getMultiview(0);
    }

    void configureExtensions(std::uint32_t contextID, bool enableMultiview)
    {
        getTextureViewSupported(contextID);
        getMultiviewSupported(contextID);

        if (enableMultiview)
        {
            getMultiview(contextID);
        }
        else
        {
            Log(Debug::Verbose) << "Disabling Multiview (disabled by config)";
        }
    }

    void setVertexBufferHint(bool enableMultiview, bool allowDisplayListsForMultiview)
    {
        if (getStereo() && enableMultiview)
        {
            auto* ds = osg::DisplaySettings::instance().get();
            if (!allowDisplayListsForMultiview
                && ds->getVertexBufferHint() == osg::DisplaySettings::VertexBufferHint::NO_PREFERENCE)
            {
                // Note that this only works if this code is executed before realize() is called on the viewer.
                // The hint is read by the state object only once, before the user realize operations are run.
                // Therefore we have to set this hint without access to a graphics context to let us determine
                // if multiview will actually be supported or not. So if the user has requested multiview, we
                // will just have to set it regardless.
                ds->setVertexBufferHint(osg::DisplaySettings::VertexBufferHint::VERTEX_BUFFER_OBJECT);
                Log(Debug::Verbose) << "Disabling display lists";
            }
        }
    }

    namespace
    {
        class Texture2DViewSubloadCallback : public osg::Texture2D::SubloadCallback
        {
        public:
            Texture2DViewSubloadCallback(osg::Texture2DArray* textureArray, std::int32_t layer);

            void load(const osg::Texture2D& texture, osg::State& state) const override;
            void subload(const osg::Texture2D& texture, osg::State& state) const override;

        private:
            osg::ref_ptr<osg::Texture2DArray> mTextureArray;
            std::int32_t mLayer;
        };
    }

    Texture2DViewSubloadCallback::Texture2DViewSubloadCallback(osg::Texture2DArray* textureArray, std::int32_t layer)
        : mTextureArray(textureArray)
        , mLayer(layer)
    {
    }

    void Texture2DViewSubloadCallback::load(const osg::Texture2D& texture, osg::State& state) const
    {
        state.checkGLErrors("before Texture2DViewSubloadCallback::load()");

        auto contextId = state.getContextID();
        auto* gl = osg::GLExtensions::Get(contextId, false);
        mTextureArray->apply(state);

        auto sourceTextureObject = mTextureArray->getTextureObject(contextId);
        if (!sourceTextureObject)
        {
            Log(Debug::Error) << "Texture2DViewSubloadCallback: Texture2DArray did not have a texture object";
            return;
        }

        osg::Texture::TextureObject* const targetTextureObject = texture.getTextureObject(contextId);
        if (targetTextureObject == nullptr)
        {
            Log(Debug::Error) << "Texture2DViewSubloadCallback: Texture2D did not have a texture object";
            return;
        }

        // OSG already bound this texture ID, giving it a target.
        // Delete it and make a new texture ID.
        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(1, &targetTextureObject->_id);
        glGenTextures(1, &targetTextureObject->_id);

        auto sourceId = sourceTextureObject->_id;
        auto targetId = targetTextureObject->_id;
        auto internalFormat = sourceTextureObject->_profile._internalFormat;
        auto levels = std::max(1, sourceTextureObject->_profile._numMipmapLevels);

        {
            ////// OSG BUG
            // Texture views require immutable storage.
            // OSG should always give immutable storage to sized internal formats, but does not do so for depth formats.
            // Fortunately, we can just call glTexStorage3D here to make it immutable. This probably discards depth info
            // for that frame, but whatever.
            // Store any current binding and re-apply it after so i don't mess with state.
            GLint oldBinding = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &oldBinding);

            // Bind the source texture and check if it's immutable.
            glBindTexture(GL_TEXTURE_2D_ARRAY, sourceId);
            GLint immutable = 0;
            glGetTexParameteriv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_IMMUTABLE_FORMAT, &immutable);
            if (!immutable)
            {
                // It wasn't immutable, so make it immutable.
                gl->glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, internalFormat, sourceTextureObject->_profile._width,
                    sourceTextureObject->_profile._height, 2);
                state.checkGLErrors("after Texture2DViewSubloadCallback::load()::glTexStorage3D");
            }
            glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(oldBinding));
        }

        gl->glTextureView(targetId, GL_TEXTURE_2D, sourceId, internalFormat, 0, static_cast<GLuint>(levels),
            static_cast<GLuint>(mLayer), 1);
        state.checkGLErrors("after Texture2DViewSubloadCallback::load()::glTextureView");
        glBindTexture(GL_TEXTURE_2D, targetId);
    }

    void Texture2DViewSubloadCallback::subload(const osg::Texture2D& texture, osg::State& state) const
    {
        // Nothing to do
    }

    osg::ref_ptr<osg::Texture2D> createTextureView_Texture2DFromTexture2DArray(
        osg::Texture2DArray* textureArray, std::int32_t layer)
    {
        if (!getTextureViewSupported())
        {
            Log(Debug::Error) << "createTextureView_Texture2DFromTexture2DArray: Tried to use a texture view but "
                                 "glTextureView is not supported";
            return nullptr;
        }

        osg::ref_ptr<osg::Texture2D> texture2d = new osg::Texture2D;
        texture2d->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        texture2d->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        texture2d->setSubloadCallback(new Texture2DViewSubloadCallback(textureArray, layer));
        texture2d->setTextureSize(textureArray->getTextureWidth(), textureArray->getTextureHeight());
        texture2d->setBorderColor(textureArray->getBorderColor());
        texture2d->setBorderWidth(textureArray->getBorderWidth());
        texture2d->setLODBias(textureArray->getLODBias());
        texture2d->setFilter(osg::Texture::FilterParameter::MAG_FILTER,
            textureArray->getFilter(osg::Texture::FilterParameter::MAG_FILTER));
        texture2d->setFilter(osg::Texture::FilterParameter::MIN_FILTER,
            textureArray->getFilter(osg::Texture::FilterParameter::MIN_FILTER));
        texture2d->setInternalFormat(textureArray->getInternalFormat());
        texture2d->setNumMipmapLevels(textureArray->getNumMipmapLevels());
        return texture2d;
    }

    void setMultiviewMSAAResolveCallback(osgUtil::RenderStage* renderStage) {}

    void setMultiviewMatrices(
        osg::StateSet* stateset, const std::array<osg::Matrix, 2>& projection, bool createInverseMatrices)
    {
        auto* projUniform = stateset->getUniform("projectionMatrixMultiView");
        if (!projUniform)
        {
            projUniform = new osg::Uniform(osg::Uniform::FLOAT_MAT4, "projectionMatrixMultiView", 2);
            stateset->addUniform(projUniform, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
        }

        projUniform->setElement(0, projection[0]);
        projUniform->setElement(1, projection[1]);

        if (createInverseMatrices)
        {
            auto* invUniform = stateset->getUniform("invProjectionMatrixMultiView");
            if (!invUniform)
            {
                invUniform = new osg::Uniform(osg::Uniform::FLOAT_MAT4, "invProjectionMatrixMultiView", 2);
                stateset->addUniform(invUniform, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
            }

            invUniform->setElement(0, osg::Matrix::inverse(projection[0]));
            invUniform->setElement(1, osg::Matrix::inverse(projection[1]));
        }
    }

    void setMultiviewCompatibleTextureSize(osg::Texture* tex, std::int32_t w, std::int32_t h)
    {
        switch (tex->getTextureTarget())
        {
            case GL_TEXTURE_2D:
                static_cast<osg::Texture2D*>(tex)->setTextureSize(w, h);
                break;
            case GL_TEXTURE_2D_ARRAY:
                static_cast<osg::Texture2DArray*>(tex)->setTextureSize(w, h, 2);
                break;
            case GL_TEXTURE_2D_MULTISAMPLE:
                static_cast<osg::Texture2DMultisample*>(tex)->setTextureSize(w, h);
                break;
            default:
                throw std::logic_error("Invalid texture type received");
        }
    }

    osg::ref_ptr<osg::Texture> createMultiviewCompatibleTexture(
        std::int32_t width, std::int32_t height, std::int32_t samples)
    {
        {
            if (samples > 1)
            {
                auto tex = new osg::Texture2DMultisample();
                tex->setTextureSize(width, height);
                tex->setNumSamples(samples);
                tex->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
                tex->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
                return tex;
            }
            else
            {
                auto tex = new osg::Texture2D();
                tex->setTextureSize(width, height);
                tex->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
                tex->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
                return tex;
            }
        }
    }

    osg::FrameBufferAttachment createMultiviewCompatibleAttachment(osg::Texture* tex)
    {
        switch (tex->getTextureTarget())
        {
            case GL_TEXTURE_2D:
            {
                auto* tex2d = static_cast<osg::Texture2D*>(tex);
                return osg::FrameBufferAttachment(tex2d);
            }
            case GL_TEXTURE_2D_MULTISAMPLE:
            {
                auto* tex2dMsaa = static_cast<osg::Texture2DMultisample*>(tex);
                return osg::FrameBufferAttachment(tex2dMsaa);
            }
            default:
                throw std::logic_error("Invalid texture type received");
        }
    }

    std::uint32_t osgFaceControlledByMultiviewShader()
    {
        return 0;
    }

    class UpdateRenderStagesCallback
        : public SceneUtil::NodeCallback<UpdateRenderStagesCallback, osg::Node*, osgUtil::CullVisitor*>
    {
    public:
        explicit UpdateRenderStagesCallback(Stereo::MultiviewFramebuffer* multiviewFramebuffer)
            : mMultiviewFramebuffer(multiviewFramebuffer)
        {
            mViewport = new osg::Viewport(0, 0, multiviewFramebuffer->width(), multiviewFramebuffer->height());
            mViewportStateset = new osg::StateSet();
            mViewportStateset->setAttribute(mViewport.get());
        }

        void operator()(osg::Node* node, osgUtil::CullVisitor* cv)
        {
            osgUtil::RenderStage* renderStage = cv->getCurrentRenderStage();

            bool msaa = mMultiviewFramebuffer->samples() > 1;

            if (!Stereo::getMultiview())
            {
                auto eye = static_cast<std::int32_t>(Stereo::Manager::instance().getEye(cv));
                if (eye < 2)
                {
                    if (msaa)
                    {
                        renderStage->setFrameBufferObject(mMultiviewFramebuffer->layerMsaaFbo(eye));
                        renderStage->setMultisampleResolveFramebufferObject(mMultiviewFramebuffer->layerFbo(eye));
                    }
                    else
                    {
                        renderStage->setFrameBufferObject(mMultiviewFramebuffer->layerFbo(eye));
                    }
                }
            }

            // OSG tries to do a horizontal split, but we want to render to separate framebuffers instead.
            renderStage->setViewport(mViewport);
            cv->pushStateSet(mViewportStateset.get());
            traverse(node, cv);
            cv->popStateSet();
        }

    private:
        Stereo::MultiviewFramebuffer* mMultiviewFramebuffer;
        osg::ref_ptr<osg::Viewport> mViewport;
        osg::ref_ptr<osg::StateSet> mViewportStateset;
    };

    MultiviewFramebuffer::MultiviewFramebuffer(std::int32_t width, std::int32_t height, std::int32_t samples)
        : mWidth(width)
        , mHeight(height)
        , mSamples(samples)
        , mMultiview(getMultiview())
        , mMultiviewFbo{ new osg::FrameBufferObject }
        , mLayerFbo{ new osg::FrameBufferObject, new osg::FrameBufferObject }
        , mLayerMsaaFbo{ new osg::FrameBufferObject, new osg::FrameBufferObject }
    {
    }

    MultiviewFramebuffer::~MultiviewFramebuffer() = default;

    void MultiviewFramebuffer::attachColorComponent(GLint sourceFormat, GLint sourceType, GLint internalFormat)
    {
        if (mMultiview)
        {
        }
        else
        {
            for (std::uint32_t i = 0; i < 2; i++)
            {
                if (mSamples > 1)
                    mLayerMsaaFbo[i]->setAttachment(osg::Camera::COLOR_BUFFER,
                        osg::FrameBufferAttachment(
                            new osg::RenderBuffer(mWidth, mHeight, static_cast<GLenum>(internalFormat), mSamples)));
                mColorTexture[i] = createTexture(sourceFormat, sourceType, internalFormat);
                mLayerFbo[i]->setAttachment(osg::Camera::COLOR_BUFFER, osg::FrameBufferAttachment(mColorTexture[i]));
            }
        }
    }

    void MultiviewFramebuffer::attachDepthComponent(GLint sourceFormat, GLint sourceType, GLint internalFormat)
    {
        if (mMultiview)
        {
        }
        else
        {
            for (std::uint32_t i = 0; i < 2; i++)
            {
                if (mSamples > 1)
                    mLayerMsaaFbo[i]->setAttachment(osg::Camera::PACKED_DEPTH_STENCIL_BUFFER,
                        osg::FrameBufferAttachment(
                            new osg::RenderBuffer(mWidth, mHeight, static_cast<GLenum>(internalFormat), mSamples)));
                mDepthTexture[i] = createTexture(sourceFormat, sourceType, internalFormat);
                mLayerFbo[i]->setAttachment(
                    osg::Camera::PACKED_DEPTH_STENCIL_BUFFER, osg::FrameBufferAttachment(mDepthTexture[i]));
            }
        }
    }

    osg::FrameBufferObject* MultiviewFramebuffer::multiviewFbo()
    {
        return mMultiviewFbo;
    }

    osg::FrameBufferObject* MultiviewFramebuffer::layerFbo(std::int32_t i)
    {
        return mLayerFbo[static_cast<std::size_t>(i)];
    }

    osg::FrameBufferObject* MultiviewFramebuffer::layerMsaaFbo(std::int32_t i)
    {
        return mLayerMsaaFbo[static_cast<std::size_t>(i)];
    }

    osg::Texture2DArray* MultiviewFramebuffer::multiviewColorBuffer()
    {
        return mMultiviewColorTexture;
    }

    osg::Texture2DArray* MultiviewFramebuffer::multiviewDepthBuffer()
    {
        return mMultiviewDepthTexture;
    }

    osg::Texture2D* MultiviewFramebuffer::layerColorBuffer(std::int32_t i)
    {
        return mColorTexture[static_cast<std::size_t>(i)];
    }

    osg::Texture2D* MultiviewFramebuffer::layerDepthBuffer(std::int32_t i)
    {
        return mDepthTexture[static_cast<std::size_t>(i)];
    }
    void MultiviewFramebuffer::attachTo(osg::Camera* camera)
    {
        camera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);

        if (!mCullCallback)
            mCullCallback = new UpdateRenderStagesCallback(this);
        camera->addCullCallback(mCullCallback);
    }

    void MultiviewFramebuffer::detachFrom(osg::Camera* camera)
    {
        camera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER);
        if (mCullCallback)
            camera->removeCullCallback(mCullCallback);
    }

    osg::Texture2D* MultiviewFramebuffer::createTexture(GLint sourceFormat, GLint sourceType, GLint internalFormat)
    {
        auto* texture = new osg::Texture2D;
        texture->setTextureSize(mWidth, mHeight);
        texture->setSourceFormat(static_cast<GLenum>(sourceFormat));
        texture->setSourceType(static_cast<GLenum>(sourceType));
        texture->setInternalFormat(internalFormat);
        texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
        texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WRAP_R, osg::Texture::CLAMP_TO_EDGE);
        return texture;
    }

    osg::Texture2DArray* MultiviewFramebuffer::createTextureArray(
        GLint sourceFormat, GLint sourceType, GLint internalFormat)
    {
        auto* textureArray = new osg::Texture2DArray;
        textureArray->setTextureSize(mWidth, mHeight, 2);
        textureArray->setSourceFormat(static_cast<GLenum>(sourceFormat));
        textureArray->setSourceType(static_cast<GLenum>(sourceType));
        textureArray->setInternalFormat(internalFormat);
        textureArray->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
        textureArray->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        textureArray->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        textureArray->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        textureArray->setWrap(osg::Texture::WRAP_R, osg::Texture::CLAMP_TO_EDGE);
        return textureArray;
    }

    namespace
    {
        osg::FrameBufferAttachment makeSingleLayerAttachmentFromMultilayerAttachment(
            osg::FrameBufferAttachment attachment, std::int32_t layer)
        {
            osg::Texture* tex = attachment.getTexture();

            if (tex->getTextureTarget() == GL_TEXTURE_2D_ARRAY)
                return osg::FrameBufferAttachment(
                    static_cast<osg::Texture2DArray*>(tex), static_cast<std::uint32_t>(layer), 0);

            Log(Debug::Error) << "Attempted to extract a layer from an unlayered texture";

            return osg::FrameBufferAttachment();
        }
    }

    MultiviewFramebufferResolve::MultiviewFramebufferResolve(
        osg::FrameBufferObject* msaaFbo, osg::FrameBufferObject* resolveFbo, GLbitfield blitMask)
        : mResolveFbo(resolveFbo)
        , mMsaaFbo(msaaFbo)
        , mBlitMask(blitMask)
    {
    }

    void MultiviewFramebufferResolve::setResolveFbo(osg::FrameBufferObject* resolveFbo)
    {
        if (resolveFbo != mResolveFbo)
            dirty();
        mResolveFbo = resolveFbo;
    }

    void MultiviewFramebufferResolve::setMsaaFbo(osg::FrameBufferObject* msaaFbo)
    {
        if (msaaFbo != mMsaaFbo)
            dirty();
        mMsaaFbo = msaaFbo;
    }

    void MultiviewFramebufferResolve::resolveImplementation(osg::State& state)
    {
        if (mDirtyLayers)
            setupLayers();

        osg::GLExtensions* ext = state.get<osg::GLExtensions>();

        for (std::int32_t view : { 0, 1 })
        {
            mResolveLayers[static_cast<std::size_t>(view)]->apply(
                state, osg::FrameBufferObject::BindTarget::DRAW_FRAMEBUFFER);
            mMsaaLayers[static_cast<std::size_t>(view)]->apply(
                state, osg::FrameBufferObject::BindTarget::READ_FRAMEBUFFER);
            ext->glBlitFramebuffer(0, 0, mWidth, mHeight, 0, 0, mWidth, mHeight, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        }
    }
    void MultiviewFramebufferResolve::setupLayers()
    {
        mDirtyLayers = false;
        std::vector<osg::FrameBufferObject::BufferComponent> components;
        if (mBlitMask & GL_DEPTH_BUFFER_BIT)
            components.push_back(osg::FrameBufferObject::BufferComponent::PACKED_DEPTH_STENCIL_BUFFER);
        if (mBlitMask & GL_COLOR_BUFFER_BIT)
            components.push_back(osg::FrameBufferObject::BufferComponent::COLOR_BUFFER);

        mMsaaLayers = { new osg::FrameBufferObject, new osg::FrameBufferObject };
        mResolveLayers = { new osg::FrameBufferObject, new osg::FrameBufferObject };
        for (auto component : components)
        {
            const auto& msaaAttachment = mMsaaFbo->getAttachment(component);
            mMsaaLayers[0]->setAttachment(
                component, makeSingleLayerAttachmentFromMultilayerAttachment(msaaAttachment, 0));
            mMsaaLayers[1]->setAttachment(
                component, makeSingleLayerAttachmentFromMultilayerAttachment(msaaAttachment, 1));

            const auto& resolveAttachment = mResolveFbo->getAttachment(component);
            mResolveLayers[0]->setAttachment(
                component, makeSingleLayerAttachmentFromMultilayerAttachment(resolveAttachment, 0));
            mResolveLayers[1]->setAttachment(
                component, makeSingleLayerAttachmentFromMultilayerAttachment(resolveAttachment, 1));

            mWidth = msaaAttachment.getTexture()->getTextureWidth();
            mHeight = msaaAttachment.getTexture()->getTextureHeight();
        }
    }

    InitialFrustumCallback::InitialFrustumCallback(osg::Camera* camera)
        : mCamera(camera)
    {
    }

    InitialFrustumCallback::~InitialFrustumCallback() {}
}
