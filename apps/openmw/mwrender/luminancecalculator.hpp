#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <osg/FrameBufferObject>
#include <osg/Program>
#include <osg/Texture2D>

namespace Shader
{
    class ShaderManager;
}

namespace MWRender
{
    class PingPongCanvas;

    class LuminanceCalculator
    {

    public:
        LuminanceCalculator() = default;

        explicit LuminanceCalculator(Shader::ShaderManager& shaderManager);

        void draw(const PingPongCanvas& canvas, osg::RenderInfo& renderInfo, osg::State& state, osg::GLExtensions* ext,
            std::size_t frameId);

        bool isEnabled() const { return mEnabled; }

        void enable() { mEnabled = true; }
        void disable() { mEnabled = false; }

        void dirty(std::int32_t w, std::int32_t h);

        osg::ref_ptr<osg::Texture2D> getLuminanceTexture(std::size_t frameId) const;

    private:
        void compile();

        struct Container
        {
            osg::ref_ptr<osg::FrameBufferObject> sceneLumFbo;
            osg::ref_ptr<osg::FrameBufferObject> resolveSceneLumFbo;
            osg::ref_ptr<osg::FrameBufferObject> resolveFbo;
            osg::ref_ptr<osg::FrameBufferObject> luminanceProxyFbo;
            osg::ref_ptr<osg::Texture2D> mipmappedSceneLuminanceTex;
            osg::ref_ptr<osg::Texture2D> luminanceTex;
            osg::ref_ptr<osg::Texture2D> luminanceProxyTex;
            osg::ref_ptr<osg::StateSet> sceneLumSS;
            osg::ref_ptr<osg::StateSet> resolveSS;
        };

        std::array<Container, 2> mBuffers;
        osg::ref_ptr<osg::Program> mLuminanceProgram;
        osg::ref_ptr<osg::Program> mResolveProgram;

        bool mCompiled = false;
        bool mEnabled = false;
        bool mIsBlank = true;

        std::int32_t mWidth = 1;
        std::int32_t mHeight = 1;
        osg::Vec2f mScale = osg::Vec2f(1, 1);
    };
}
