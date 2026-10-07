#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include <osg/Image>
#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <components/rtx/common/namedenum.hpp>
#include <components/rtx/image/colour.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/scene/surface.hpp>

namespace osg
{
    class Texture;
}

namespace Rtx
{
    /// What of a surface's state the trace does not read, one bit each: a fact the content stated
    /// that the rasterizer would draw and the trace does not. `MaterialResolver` reports each as a
    /// refusal, once for each texture a surface names, and draws the rest of the surface.
    enum class UnreadState : std::uint32_t
    {
        /// A detail, decal, gloss or bump map, which no shipped file binds.
        Role = 1u << 0,

        /// A blend other than the four the shipped files state.
        BlendPair = 1u << 1,
        BlendEquation = 1u << 2,

        /// A cull of the front faces.
        CulledFront = 1u << 3,

        /// Lines or points in place of faces.
        PolygonMode = 1u << 4,

        /// A fog of the surface's own, an `NiFogProperty`.
        Fog = 1u << 5,

        Stencil = 1u << 6,
        ColourMask = 1u << 7,
        FlatShading = 1u << 8,

        /// A texture unit's own state: an environment mode, a coordinate generator, a matrix.
        TextureState = 1u << 9,

        /// An attribute or a mode this reader has no rule for.
        Unknown = 1u << 10,
    };

    inline constexpr std::array sUnreadStates{ UnreadState::Role, UnreadState::BlendPair, UnreadState::BlendEquation,
        UnreadState::CulledFront, UnreadState::PolygonMode, UnreadState::Fog, UnreadState::Stencil,
        UnreadState::ColourMask, UnreadState::FlatShading, UnreadState::TextureState, UnreadState::Unknown };

    /// Why the trace draws a surface that states `state` otherwise: the reason its refusal gives.
    std::string_view whyUnread(UnreadState state);

    /// What a texture is for, as the content file said. A role and not a texture unit: where a unit
    /// is bound, and whether there are units at all, is a renderer's business.
    enum class TextureRole
    {
        Diffuse,
        Normal,

        /// A normal map carrying height in its alpha, for parallax. The same slot as `Normal` to a
        /// renderer that does not do parallax.
        NormalHeight,

        Emissive,
        Specular,
        Dark,

        /// Named so a unit that carries one is a role and not an unknown, and read by nothing:
        /// none of the four occurs in the shipped files, over every NIF the three archives hold.
        Detail,
        Decal,
        Gloss,
        Bump,

        Environment,
    };

    /// The name the OpenGL renderer binds each role under — one table, so a typo is a build error
    /// rather than an untextured surface. A name that is not a role reads as none: `blendMap` and
    /// the shadow maps are bound the same way and are not what a surface is made of.
    inline constexpr NamedEnum sTextureRoleNames{ std::array{
        std::pair{ TextureRole::Diffuse, std::string_view("diffuseMap") },
        std::pair{ TextureRole::Normal, std::string_view("normalMap") },
        std::pair{ TextureRole::NormalHeight, std::string_view("normalHeightMap") },
        std::pair{ TextureRole::Emissive, std::string_view("emissiveMap") },
        std::pair{ TextureRole::Specular, std::string_view("specularMap") },
        std::pair{ TextureRole::Dark, std::string_view("darkMap") },
        std::pair{ TextureRole::Detail, std::string_view("detailMap") },
        std::pair{ TextureRole::Decal, std::string_view("decalMap") },
        std::pair{ TextureRole::Gloss, std::string_view("glossMap") },
        std::pair{ TextureRole::Bump, std::string_view("bumpMap") },
        std::pair{ TextureRole::Environment, std::string_view("envMap") },
    } };

    /// The maps a surface keeps: the roles the trace reads. `TextureRole` names what the content
    /// can bind, so a unit can be told from one that is no role at all; this names what is
    /// recorded, because the detail, decal, gloss and bump roles occur in no shipped file, and a
    /// fold that kept them anyway paid a reference apiece for nothing.
    enum class SurfaceMap : std::uint8_t
    {
        Diffuse,
        Emissive,
        Dark,
        Environment,

        /// A tangent-space normal map, with or without height in its alpha.
        Normal,

        /// A specular map, whose channels `Rtx::SpecularLayout` says the meaning of.
        Specular,

        Count,
    };

    inline constexpr std::size_t sSurfaceMapCount = static_cast<std::size_t>(SurfaceMap::Count);

    /// The map a role is kept as, or nothing for a role the trace declines to read.
    constexpr std::optional<SurfaceMap> mapOf(const TextureRole role)
    {
        switch (role)
        {
            case TextureRole::Diffuse:
                return SurfaceMap::Diffuse;
            case TextureRole::Emissive:
                return SurfaceMap::Emissive;
            case TextureRole::Dark:
                return SurfaceMap::Dark;
            case TextureRole::Environment:
                return SurfaceMap::Environment;
            case TextureRole::Normal:
            case TextureRole::NormalHeight:
                return SurfaceMap::Normal;
            case TextureRole::Specular:
                return SurfaceMap::Specular;
            case TextureRole::Detail:
            case TextureRole::Decal:
            case TextureRole::Gloss:
            case TextureRole::Bump:
                break;
        }
        return std::nullopt;
    }

    /// One texture as a surface uses it: the image, and how it is addressed past its edges.
    ///
    /// **The image and not an `osg::Texture2D`**, for the reason `SurfaceDescription::mTextures`
    /// gives; **and the wrap beside it**, because that is the one piece of sampler state the
    /// content decides per texture. A `NiSourceTexture` states a clamp, `NifOsg` puts it on the
    /// texture, and a renderer that read the image alone repeated every banner's edge.
    struct TextureUse
    {
        osg::ref_ptr<const osg::Image> mImage;
        TextureWrap mWrap = TextureWrap::Repeat;

        const osg::Image* get() const { return mImage.get(); }

        bool operator==(const TextureUse& other) const = default;
    };

    /// What one texel of a sheet adds on average under `blend`: weighted by its own alpha where the
    /// blend reads one, and whole where `AddWhole` reads none. One rule for whatever draws the
    /// sheet, a material's surface or an emitter's sprites.
    inline osg::Vec3f meanUnder(const MeanTexel& mean, const BlendKind blend)
    {
        return blend == BlendKind::AddWhole ? mean.mWhole : mean.mColour;
    }

    /// What a surface is, as the content said and before any renderer has an opinion. Read off the
    /// finished `osg::StateSet` by `describeStateSet`, because that is the one place whoever loaded
    /// the content kept the fact, whatever a controller has done to it since. A value, and cheap to
    /// copy: a chain of state sets is folded into one of these in order.
    struct SurfaceDescription
    {
        /// One texture per map the trace reads, null where the content has none. The image and
        /// not an `osg::Texture2D`: `osgDB::SharedStateManager` replaces the texture `NifOsg` bound
        /// by one it never saw, and the image is what `Resource::ImageManager` caches by path and
        /// what carries the file name a renderer identifies a texture by. The wrap comes across
        /// with it, and the rest of the sampler state stays on the texture.
        std::array<TextureUse, sSurfaceMapCount> mTextures;

        /// How a blended surface composites, meaningful under `AlphaMode::Blend`.
        BlendKind mBlend = BlendKind::Over;

        /// Whether the normal map's alpha is a height, for parallax: bound as `normalHeightMap` — an
        /// `_nh` file — and `carriesHeight`. The nearest state set's normal map says, as it says
        /// which map it is.
        bool mNormalHeight = false;

        /// Which texture unit the dark map and the glow map are bound at, each meaningful where
        /// there is one. Carried because half the vanilla dark maps and some glow maps read the
        /// geometry's second set of texture coordinates, and which set a unit reads is the
        /// geometry's to say.
        std::uint8_t mDarkUnit = 0;
        std::uint8_t mEmissiveUnit = 0;

        /// What the environment map is tinted by: `envMapColor`, which `NifOsg` sets to white
        /// under a `NiTextureEffect` and `SceneUtil::GlowUpdater` to the enchantment's colour.
        EncodedColour mEnvironmentColour{ 1.0f, 1.0f, 1.0f };

        /// The ambient light the game overrides for this surface, or nothing. `EffectManager` and
        /// `Animation::addEffect` give a magic effect a white `sun.ambient`, which is what makes
        /// it fully lit in a cave; the rasterizer sums it with the material's ambient colour.
        std::optional<EncodedColour> mAmbientOverride;

        AlphaMode mAlphaMode = AlphaMode::Opaque;

        /// What the surface's per-vertex colour is for: a property of the surface, where the
        /// colours are a property of the geometry, and constant for the material's life while the
        /// controllers rewrite the colours beside it.
        VertexColour mVertexColour = VertexColour::None;

        /// What `Cutout` cuts by. Meaningful whenever the content asked for alpha testing, which
        /// includes surfaces that also blend; `ALWAYS` passes on every side.
        AlphaTest mAlphaTest{};

        /// The `UnreadState`s the chain states, as bits: a fact a nearer state set restates is the
        /// nearer one's, as every other fact here is.
        std::uint32_t mUnread = 0;

        bool isUnread(UnreadState state) const { return (mUnread & static_cast<std::uint32_t>(state)) != 0; }

        void markUnread(UnreadState state, bool unread)
        {
            const auto bit = static_cast<std::uint32_t>(state);
            mUnread = unread ? mUnread | bit : mUnread & ~bit;
        }

        /// Whether both faces of this surface are drawn and lit. False unless the content says
        /// otherwise, because the scene root turns `GL_CULL_FACE` on for everything under it, and
        /// only a `NiStencilProperty` drawing `Both` or a material file's two-sided flag turns it
        /// off. Vanilla Morrowind has neither: a leaf meant to be seen from behind is a second copy
        /// wound the other way, which is `Rtx::ShapeFold`'s business.
        bool mTwoSided = false;

        /// Whether the lamps light this surface. Every surface the content describes is lit by
        /// them; upstream's groundcover alone takes them away, where `[Groundcover] point lighting`
        /// is off (`Rtx::CellReader::readModel`).
        bool mLampLit = true;

        /// Three of the four colours a `NiMaterialProperty` states for a surface, display-encoded.
        /// The specular and the glossiness beside them are not read: `NifOsg` sets the specular to
        /// black on every Morrowind NIF, because the game had specular lighting disabled. A surface's
        /// specular is its `SurfaceMap::Specular`, or none.
        EncodedColour mDiffuseColour{ 1.0f, 1.0f, 1.0f };
        EncodedColour mAmbientColour{ 1.0f, 1.0f, 1.0f };
        EncodedColour mEmissiveColour;

        /// How much of the surface is there, before its texture is read. Beside the diffuse colour
        /// and not inside it, because that is where `NiMaterialProperty` keeps it and
        /// `NifOsg::AlphaController` animates this one field alone.
        float mOpacity = 1.0f;

        /// A separate multiplier rather than folded into `mEmissiveColour`, because a
        /// `NiMaterialColorController` animates the colour and leaves this alone.
        float mEmissiveMult = 1.0f;

        /// How texture coordinates are transformed before the surface is sampled: scaled about the
        /// middle of the texture, then offset. The resolved translation, because a
        /// `BSShaderProperty` and a `NiUVController` negate different components of what they
        /// record. `NifOsg::UVController` rewrites this every frame it is applied.
        osg::Vec2f mTextureScale{ 1.0f, 1.0f };
        osg::Vec2f mTextureOffset{ 0.0f, 0.0f };

        const osg::Image* getTexture(SurfaceMap map) const { return mTextures[static_cast<std::size_t>(map)].get(); }

        const TextureUse& getTextureUse(SurfaceMap map) const { return mTextures[static_cast<std::size_t>(map)]; }

        /// Repeating, which is what a caller that has no texture to read a wrap off means.
        void setTexture(SurfaceMap map, const osg::Image* image, TextureWrap wrap = TextureWrap::Repeat)
        {
            mTextures[static_cast<std::size_t>(map)] = TextureUse{ .mImage = image, .mWrap = wrap };
        }

        /// The same, taking whatever the texture was bound as, its wrap included. Null and
        /// imageless textures clear the map, which is what a placeholder a flip controller has
        /// not filled in yet amounts to.
        void setTexture(SurfaceMap map, const osg::Texture* texture);

        /// Whether the surface gives light of its own, which the trace adds past the light it
        /// receives (`litSurface`): an emissive colour, an emissive map, colours that are a glow,
        /// the ambient a magic effect overrides, or an additive blend.
        bool emits() const
        {
            const bool coloured = mEmissiveMult > 0.0f && mEmissiveColour != EncodedColour{};
            return coloured || getTexture(SurfaceMap::Emissive) != nullptr || mVertexColour == VertexColour::Glow
                || mAmbientOverride.has_value() || additiveSurface(mAlphaMode, mBlend);
        }

        bool operator==(const SurfaceDescription& other) const = default;
    };
}
