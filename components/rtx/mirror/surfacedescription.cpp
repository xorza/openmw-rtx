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
            case UnreadState::DetailMap:
                return "a detail map is not read";
            case UnreadState::DecalMap:
                return "a decal map is not read";
            case UnreadState::GlossMap:
                return "a gloss map is not read";
            case UnreadState::BumpMap:
                return "a bump map is not read";
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

    TextureEncoding SurfaceDescription::encodingOf(const SurfaceMap map, const SpecularLayout specular) const
    {
        const bool paired = getTexture(SurfaceMap::Diffuse) != nullptr && getTexture(SurfaceMap::Specular) != nullptr;
        const bool classic = paired && specular == SpecularLayout::Classic;
        switch (map)
        {
            case SurfaceMap::Normal:
                return TextureEncoding::Normal;
            case SurfaceMap::Specular:
                return classic ? TextureEncoding::ConvertedSpecular : TextureEncoding::Data;
            case SurfaceMap::Diffuse:
                if (classic)
                    return TextureEncoding::ConvertedColour;
                return getTexture(SurfaceMap::Specular) != nullptr ? TextureEncoding::Colour : TextureEncoding::Painted;
            default:
                return TextureEncoding::Colour;
        }
    }

    const osg::Image* SurfaceDescription::partnerOf(const SurfaceMap map, const SpecularLayout specular) const
    {
        switch (encodingOf(map, specular))
        {
            case TextureEncoding::ConvertedColour:
                return getTexture(SurfaceMap::Specular);
            case TextureEncoding::ConvertedSpecular:
                return getTexture(SurfaceMap::Diffuse);
            default:
                return nullptr;
        }
    }
}
