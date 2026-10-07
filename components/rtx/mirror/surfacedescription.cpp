#include "surfacedescription.hpp"

#include <cstddef>
#include <string_view>

#include <osg/Texture>

namespace Rtx
{
    namespace
    {
        /// Whether a texture's wrap mode clamps. `CLAMP`, `CLAMP_TO_EDGE` and `CLAMP_TO_BORDER` are
        /// three spellings of one edge; `MIRROR` does not occur in the content and repeats.
        bool clamps(const osg::Texture::WrapMode mode)
        {
            return mode == osg::Texture::CLAMP || mode == osg::Texture::CLAMP_TO_EDGE
                || mode == osg::Texture::CLAMP_TO_BORDER;
        }
    }

    void SurfaceDescription::setTexture(SurfaceMap map, const osg::Texture* texture)
    {
        TextureUse& use = mTextures[static_cast<std::size_t>(map)];
        if (texture == nullptr)
        {
            use = TextureUse{};
            return;
        }

        use.mImage = texture->getImage(0);
        use.mWrap = textureWrapOf(
            clamps(texture->getWrap(osg::Texture::WRAP_S)), clamps(texture->getWrap(osg::Texture::WRAP_T)));
    }

    std::string_view whyUnread(const UnreadState state)
    {
        switch (state)
        {
            case UnreadState::Role:
                return "a detail, decal, gloss or bump map is not read";
            case UnreadState::BlendPair:
                return "a blend other than the shipped four is drawn as the nearest of them";
            case UnreadState::BlendEquation:
                return "a blend equation other than addition is drawn as addition";
            case UnreadState::CulledFront:
                return "a cull of the front faces is drawn as a cull of the back";
            case UnreadState::PolygonMode:
                return "lines or points are drawn as faces";
            case UnreadState::Fog:
                return "a fog of its own is not read; the air along the ray is";
            case UnreadState::Stencil:
                return "a stencil test is not read";
            case UnreadState::ColourMask:
                return "a colour mask is not read, so a surface that writes no colour is drawn";
            case UnreadState::FlatShading:
                return "flat shading is drawn smooth";
            case UnreadState::TextureState:
                return "a texture unit's environment mode, coordinate generator or matrix is not read";
            case UnreadState::Unknown:
                return "an attribute or mode the material reader has no rule for is not read";
        }
        return {};
    }
}
