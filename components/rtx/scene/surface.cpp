#include "surface.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <osg/AlphaFunc>
#include <osg/BlendEquation>
#include <osg/BlendFunc>
#include <osg/ColorMask>
#include <osg/CullFace>
#include <osg/FrontFace>
#include <osg/GL>
#include <osg/Material>
#include <osg/Matrixf>
#include <osg/Multisample>
#include <osg/PolygonMode>
#include <osg/ShadeModel>
#include <osg/StateAttribute>
#include <osg/StateSet>
#include <osg/Texture>
#include <osg/Uniform>
#include <osg/Vec4f>

#include <components/rtx/image/texels.hpp>
#include <components/sceneutil/material.hpp>
#include <components/sceneutil/texturetype.hpp>
#include <components/sceneutil/util.hpp>

namespace Rtx
{
    namespace
    {
        /// The three modes a NIF can state map one for one. The other three are `SceneUtil::Material`'s
        /// alone — nothing here writes them — and ambient or diffuse on its own still tints the one
        /// albedo, while a specular the renderer has not got is nothing.
        VertexColour vertexColourOf(SceneUtil::VertexColorModes mode)
        {
            switch (mode)
            {
                case SceneUtil::VertexColorModes::Emission:
                    return VertexColour::Glow;
                case SceneUtil::VertexColorModes::AmbientAndDiffuse:
                case SceneUtil::VertexColorModes::Ambient:
                case SceneUtil::VertexColorModes::Diffuse:
                    return VertexColour::Tint;
                case SceneUtil::VertexColorModes::None:
                case SceneUtil::VertexColorModes::Specular:
                    break;
            }

            return VertexColour::None;
        }

        EncodedColour stated(const osg::Vec4f& colour)
        {
            return EncodedColour{ colour.r(), colour.g(), colour.b() };
        }

        /// The role a sampler uniform gives `unit`, or nothing where none names it. The terrain
        /// binds its layers this way — `diffuseMap = 0`, `normalMap = 2` — with no `TextureType`
        /// beside them.
        std::optional<TextureRole> roleBySampler(const osg::StateSet& stateSet, const unsigned int unit)
        {
            for (const auto& [name, uniform] : stateSet.getUniformList())
            {
                const osg::Uniform* candidate = uniform.first.get();
                if (candidate == nullptr || candidate->getType() != osg::Uniform::INT)
                    continue;

                int bound = -1;
                if (!candidate->get(bound) || bound != static_cast<int>(unit))
                    continue;

                if (const std::optional<TextureRole> role = sTextureRoleNames.named(name))
                    return role;
            }

            return std::nullopt;
        }

        /// A uniform by name and the flags it was set with, without building a `std::string` where
        /// the list is empty — which it is on nearly every state set a walk meets.
        const osg::StateSet::RefUniformPair* uniformNamed(const osg::StateSet& stateSet, std::string_view name)
        {
            const osg::StateSet::UniformList& list = stateSet.getUniformList();
            if (list.empty())
                return nullptr;

            const auto found = list.find(std::string(name));
            return found != list.end() ? &found->second : nullptr;
        }

        /// Whether a texture's wrap mode clamps. `CLAMP`, `CLAMP_TO_EDGE` and `CLAMP_TO_BORDER` are
        /// three spellings of one edge; `MIRROR` does not occur in the content and repeats.
        bool clamps(const osg::Texture::WrapMode mode)
        {
            return mode == osg::Texture::CLAMP || mode == osg::Texture::CLAMP_TO_EDGE
                || mode == osg::Texture::CLAMP_TO_BORDER;
        }

        /// How a `BlendFunc` composites. A destination of `ONE` adds, and so does `DST_ALPHA`,
        /// because the frame's alpha is one; a source of `ONE` adds the colour whole.
        BlendKind blendKindOf(const osg::BlendFunc& blend)
        {
            const bool adds
                = blend.getDestination() == osg::BlendFunc::ONE || blend.getDestination() == osg::BlendFunc::DST_ALPHA;
            if (!adds)
                return BlendKind::Over;

            return blend.getSource() == osg::BlendFunc::ONE ? BlendKind::AddWhole : BlendKind::Add;
        }

        /// Whether a `BlendFunc` is one of the four blends `blendKindOf` reads: the four the
        /// shipped files state. Any other — a multiply, `DST_COLOR, ZERO` — composites otherwise.
        bool readsBlend(const osg::BlendFunc& blend)
        {
            const GLenum source = blend.getSource();
            const GLenum destination = blend.getDestination();
            return (source == osg::BlendFunc::SRC_ALPHA
                       && (destination == osg::BlendFunc::ONE_MINUS_SRC_ALPHA || destination == osg::BlendFunc::ONE
                           || destination == osg::BlendFunc::DST_ALPHA))
                || (source == osg::BlendFunc::ONE && destination == osg::BlendFunc::ONE);
        }

        /// What one attribute of a state set says that this reader does not carry. **Every type
        /// OpenSceneGraph has is named**, so a type it adds stops the build until someone decides
        /// it. Three answers: carried by the reader above; of the raster pipeline and not of a
        /// surface, which a ray has no use for; or unread, which the material reports.
        void readAttribute(const osg::StateAttribute& attribute, SurfaceDescription& material)
        {
            // The loader's own type, past OpenSceneGraph's: the role a unit is bound as, which the
            // texture loop reads.
            const osg::StateAttribute::Type type = attribute.getType();
            if (type == SceneUtil::TextureType::AttributeType)
                return;

            switch (type)
            {
                case osg::StateAttribute::TEXTURE:
                case osg::StateAttribute::MATERIAL:
                case osg::StateAttribute::ALPHAFUNC:
                    return;

                case osg::StateAttribute::BLENDFUNC:
                    material.markUnread(
                        UnreadState::BlendPair, !readsBlend(static_cast<const osg::BlendFunc&>(attribute)));
                    return;
                case osg::StateAttribute::BLENDEQUATION:
                    material.markUnread(UnreadState::BlendEquation,
                        static_cast<const osg::BlendEquation&>(attribute).getEquationRGB()
                            != osg::BlendEquation::FUNC_ADD);
                    return;
                case osg::StateAttribute::CULLFACE:
                    material.markUnread(UnreadState::CulledFront,
                        static_cast<const osg::CullFace&>(attribute).getMode() != osg::CullFace::BACK);
                    return;
                // **The winding a mirror turns round**: `SceneUtil::attach` states a clockwise front
                // over a left body part it builds under a scale of minus one, which traversal,
                // reading the winding in the mesh, already shows the right face of. A content
                // file's own clockwise front looks the same from here.
                case osg::StateAttribute::FRONTFACE:
                    return;
                case osg::StateAttribute::POLYGONMODE:
                    material.markUnread(UnreadState::PolygonMode,
                        static_cast<const osg::PolygonMode&>(attribute).getMode(osg::PolygonMode::FRONT)
                            != osg::PolygonMode::FILL);
                    return;
                case osg::StateAttribute::COLORMASK:
                {
                    const auto& mask = static_cast<const osg::ColorMask&>(attribute);
                    material.markUnread(
                        UnreadState::ColourMask, !mask.getRedMask() || !mask.getGreenMask() || !mask.getBlueMask());
                    return;
                }
                case osg::StateAttribute::SHADEMODEL:
                    material.markUnread(UnreadState::FlatShading,
                        static_cast<const osg::ShadeModel&>(attribute).getMode() == osg::ShadeModel::FLAT);
                    return;
                case osg::StateAttribute::FOG:
                    material.markUnread(UnreadState::Fog, true);
                    return;
                case osg::StateAttribute::STENCIL:
                    material.markUnread(UnreadState::Stencil, true);
                    return;
                case osg::StateAttribute::TEXENV:
                case osg::StateAttribute::TEXGEN:
                case osg::StateAttribute::TEXMAT:
                    material.markUnread(UnreadState::TextureState, true);
                    return;

                // Of the raster pipeline: what is written where, in what order, through what
                // program, at what precision. A ray meets the surface whatever these say.
                case osg::StateAttribute::DEPTH:
                case osg::StateAttribute::POLYGONOFFSET:
                case osg::StateAttribute::PROGRAM:
                case osg::StateAttribute::LIGHTMODEL:
                case osg::StateAttribute::TEXENVFILTER:
                case osg::StateAttribute::MULTISAMPLE:
                case osg::StateAttribute::ANTIALIAS:
                case osg::StateAttribute::HINT:
                case osg::StateAttribute::VIEWPORT:
                case osg::StateAttribute::SCISSOR:
                case osg::StateAttribute::VIEWPORTINDEXED:
                case osg::StateAttribute::SCISSORINDEXED:
                case osg::StateAttribute::DEPTHRANGEINDEXED:
                case osg::StateAttribute::CLIPCONTROL:
                case osg::StateAttribute::CLAMPCOLOR:
                case osg::StateAttribute::SAMPLEMASKI:
                case osg::StateAttribute::PRIMITIVERESTARTINDEX:
                case osg::StateAttribute::FRAME_BUFFER_OBJECT:
                case osg::StateAttribute::UNIFORMBUFFERBINDING:
                case osg::StateAttribute::TRANSFORMFEEDBACKBUFFERBINDING:
                case osg::StateAttribute::ATOMICCOUNTERBUFFERBINDING:
                case osg::StateAttribute::SHADERSTORAGEBUFFERBINDING:
                case osg::StateAttribute::INDIRECTDRAWBUFFERBINDING:
                case osg::StateAttribute::BINDIMAGETEXTURE:
                case osg::StateAttribute::SAMPLER:
                case osg::StateAttribute::PATCH_PARAMETER:
                case osg::StateAttribute::VERTEX_ATTRIB_DIVISOR:
                case osg::StateAttribute::VALIDATOR:
                case osg::StateAttribute::VIEWMATRIXEXTRACTOR:
                case osg::StateAttribute::CAPABILITY:
                    return;

                // Facts of a surface the trace has no rule for: lights bound by hand, line and
                // point sizes, stipples, logic operations, colour tables and matrices, clip planes,
                // the fixed-function programs and the vendors' extensions.
                case osg::StateAttribute::LIGHT:
                case osg::StateAttribute::POINT:
                case osg::StateAttribute::POINTSPRITE:
                case osg::StateAttribute::LINEWIDTH:
                case osg::StateAttribute::LINESTIPPLE:
                case osg::StateAttribute::POLYGONSTIPPLE:
                case osg::StateAttribute::LOGICOP:
                case osg::StateAttribute::COLORTABLE:
                case osg::StateAttribute::COLORMATRIX:
                case osg::StateAttribute::BLENDCOLOR:
                case osg::StateAttribute::CLIPPLANE:
                case osg::StateAttribute::VERTEXPROGRAM:
                case osg::StateAttribute::FRAGMENTPROGRAM:
                case osg::StateAttribute::OSGNV_PARAMETER_BLOCK:
                case osg::StateAttribute::OSGNVEXT_TEXTURE_SHADER:
                case osg::StateAttribute::OSGNVEXT_VERTEX_PROGRAM:
                case osg::StateAttribute::OSGNVEXT_REGISTER_COMBINERS:
                case osg::StateAttribute::OSGNVCG_PROGRAM:
                case osg::StateAttribute::OSGNVSLANG_PROGRAM:
                case osg::StateAttribute::OSGNVPARSE_PROGRAM_PARSER:
                    material.markUnread(UnreadState::Unknown, true);
                    return;
            }

            // A type past OpenSceneGraph's own that is not the loader's either.
            material.markUnread(UnreadState::Unknown, true);
        }

        /// The same for a mode: the ones the reader carries, the ones the loader and the scene set
        /// for the raster pipeline alone, and every other unread.
        void readMode(const osg::StateAttribute::GLMode mode, SurfaceDescription& material)
        {
            switch (mode)
            {
                case GL_BLEND:
                case GL_CULL_FACE:
                case GL_ALPHA_TEST:
                case GL_DEPTH_TEST:
                case GL_POLYGON_OFFSET_FILL:
                case GL_POLYGON_OFFSET_LINE:
                case GL_POLYGON_OFFSET_POINT:
                case GL_LIGHTING:
                case GL_NORMALIZE:
                case GL_RESCALE_NORMAL:
                case GL_MULTISAMPLE_ARB:
                case GL_SAMPLE_ALPHA_TO_COVERAGE_ARB:
                    return;
                case GL_FOG:
                    material.markUnread(UnreadState::Fog, true);
                    return;
                case GL_STENCIL_TEST:
                    material.markUnread(UnreadState::Stencil, true);
                    return;
                default:
                    material.markUnread(UnreadState::Unknown, true);
                    return;
            }
        }

        void readColours(const osg::StateAttribute& attribute, SurfaceDescription& material)
        {
            if (const auto* own = dynamic_cast<const SceneUtil::Material*>(&attribute))
            {
                const osg::Vec4f diffuse = own->getDiffuse();
                const SceneUtil::VertexColorModes mode = own->getVertexColorMode();

                // Under these two the rasterizer's diffuse is the vertex colour whole, its alpha
                // with it (`getDiffuseColor`), and the mesh reader reads every vertex alpha as one.
                const bool vertexDiffuse = mode == SceneUtil::VertexColorModes::AmbientAndDiffuse
                    || mode == SceneUtil::VertexColorModes::Diffuse;

                material.mDiffuseColour = stated(diffuse);
                material.mOpacity = vertexDiffuse ? 1.0f : diffuse.a();
                material.mAmbientColour = stated(own->getAmbient());
                material.mEmissiveColour = stated(own->getEmission());
                material.mEmissiveMult = own->getEmissiveMultiplier();
                material.mVertexColour = vertexColourOf(mode);
                return;
            }

            // A state set assembled by hand rather than loaded — the character doll's default
            // material — carries OpenSceneGraph's own, which has no emissive multiplier and no
            // vertex-colour mode of the content's kind.
            if (const auto* plain = dynamic_cast<const osg::Material*>(&attribute))
            {
                const osg::Vec4f diffuse = plain->getDiffuse(osg::Material::FRONT);
                material.mDiffuseColour = stated(diffuse);
                material.mOpacity = diffuse.a();
                material.mAmbientColour = stated(plain->getAmbient(osg::Material::FRONT));
                material.mEmissiveColour = stated(plain->getEmission(osg::Material::FRONT));
            }
        }

        void readTransform(
            const osg::StateSet& stateSet, const unsigned int unit, SurfaceDescription& material, SurfaceLocks& locks)
        {
            constexpr std::string_view prefix = "texMat";
            std::array<char, prefix.size() + 10> name{};
            prefix.copy(name.data(), prefix.size());
            const char* const end = std::to_chars(name.data() + prefix.size(), name.data() + name.size(), unit).ptr;

            const osg::StateSet::RefUniformPair* uniform = uniformNamed(stateSet, std::string_view(name.data(), end));
            if (uniform == nullptr || !SurfaceLocks::takes(locks.mTextureMatrix, uniform->second))
                return;

            osg::Matrixf transform;
            if (!uniform->first->get(transform))
                return;

            // The matrix `NifOsg::UVController` builds: scaled about the middle of the texture and
            // then offset, `(p - o) * s + o + t` with `o` at a half. Undone here to the two numbers
            // it was built from, which are what a renderer that samples rather than binds wants.
            const float scaleU = transform(0, 0);
            const float scaleV = transform(1, 1);
            material.mTextureScale = osg::Vec2f(scaleU, scaleV);
            material.mTextureOffset
                = osg::Vec2f(transform(3, 0) - 0.5f * (1.0f - scaleU), transform(3, 1) - 0.5f * (1.0f - scaleV));
        }
    }

    bool SurfaceLocks::takesTexture(const TextureRole role, const osg::StateAttribute::OverrideValue flags)
    {
        const std::uint32_t bit = 1u << static_cast<std::uint32_t>(role);
        bool lock = (mTextures & bit) != 0;
        const bool taken = takes(lock, flags);
        if (lock)
            mTextures |= bit;
        return taken;
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

    bool describeStateSet(const osg::StateSet& stateSet, SurfaceDescription& material)
    {
        SurfaceLocks locks;
        return describeStateSet(stateSet, material, locks);
    }

    bool describeStateSet(const osg::StateSet& stateSet, SurfaceDescription& material, SurfaceLocks& locks)
    {
        bool said = false;

        if (const osg::StateSet::RefAttributePair* colours = stateSet.getAttributePair(osg::StateAttribute::MATERIAL))
        {
            if (SurfaceLocks::takes(locks.mMaterial, colours->second))
                readColours(*colours->first, material);
            said = true;
        }

        std::optional<unsigned int> diffuseUnit;
        const osg::StateSet::TextureAttributeList& units = stateSet.getTextureAttributeList();
        for (unsigned int unit = 0; unit < units.size(); ++unit)
        {
            const osg::StateSet::RefAttributePair* pair
                = stateSet.getTextureAttributePair(unit, osg::StateAttribute::TEXTURE);
            const osg::Texture* texture = pair != nullptr ? pair->first->asTexture() : nullptr;
            if (texture == nullptr)
                continue;

            // **A unit nothing names is not a surface's**, whatever the shader visitor makes of unit
            // nought. Every loader names what it binds — `NifOsg` with a `TextureType`, the terrain
            // with a sampler uniform — and the hand-built state sets that do not are the
            // rasterizer's own effects: the water's ripple particles, the sky's dome. The one such
            // picture both renderers draw, the rain, is named where it is built.
            std::optional<TextureRole> role
                = sTextureRoleNames.named(SceneUtil::getTextureType(stateSet, *texture, unit));
            if (!role.has_value())
                role = roleBySampler(stateSet, unit);
            if (!role.has_value())
                continue;

            said = true;

            // A role the trace declines is still a role — it said the surface is one — and is
            // kept nowhere, so no lock is worth taking for it.
            const std::optional<SurfaceMap> map = mapOf(*role);
            if (!map.has_value())
                material.markUnread(UnreadState::Role, true);
            if (!map.has_value() || !locks.takesTexture(*role, pair->second))
                continue;

            material.setTexture(*map, texture);
            if (*map == SurfaceMap::Diffuse)
                diffuseUnit = unit;
            if (*map == SurfaceMap::Normal)
                material.mNormalHeight = *role == TextureRole::NormalHeight && texture->getImage(0) != nullptr
                    && carriesHeight(*texture->getImage(0));
            if (*map == SurfaceMap::Dark)
                material.mDarkUnit = static_cast<std::uint8_t>(unit);
            if (*map == SurfaceMap::Emissive)
                material.mEmissiveUnit = static_cast<std::uint8_t>(unit);
        }

        // **The alpha test, its reference from wherever the visitor left it.** `Shader::ShaderVisitor`
        // replaces the attribute with a `RemovedAlphaFunc` of the same function at a default
        // threshold and carries the real one in a uniform, so the uniform is asked first and the
        // attribute answers where no visitor has run. The function is OpenGL's set of sides past
        // `GL_NEVER`: `ALWAYS` is no test at all, which is what the scene root wears, and Morrowind's
        // own is `GREATER`.
        if (const osg::StateSet::RefAttributePair* tested = stateSet.getAttributePair(osg::StateAttribute::ALPHAFUNC);
            tested != nullptr && SurfaceLocks::takes(locks.mAlphaFunc, tested->second))
        {
            const auto* alpha = static_cast<const osg::AlphaFunc*>(tested->first.get());
            float reference = alpha->getReferenceValue();
            if (const osg::StateSet::RefUniformPair* carried = uniformNamed(stateSet, "alphaRef"))
                carried->first->get(reference);

            // A test that cuts nothing is no test, and is one value whatever its reference: two
            // surfaces that differ in nothing else are one material.
            static_assert(osg::AlphaFunc::ALWAYS - osg::AlphaFunc::NEVER == Shaders::ALPHA_PASSES_ALL);
            const AlphaTest stated{
                .mReference = reference,
                .mPasses = static_cast<std::uint32_t>(alpha->getFunction() - osg::AlphaFunc::NEVER),
            };
            material.mAlphaTest = stated.cuts() ? stated : AlphaTest{};
            if (material.mAlphaMode != AlphaMode::Blend)
                material.mAlphaMode = material.mAlphaTest.cuts() ? AlphaMode::Cutout : AlphaMode::Opaque;
        }

        // Blending wins over testing, and the threshold survives for a renderer that would rather cut.
        // The function says how the blend composites; a mode alone says only that it does.
        if (const osg::StateSet::RefAttributePair* blended = stateSet.getAttributePair(osg::StateAttribute::BLENDFUNC))
        {
            if (SurfaceLocks::takes(locks.mBlend, blended->second))
            {
                material.mAlphaMode = AlphaMode::Blend;
                material.mBlend = blendKindOf(*static_cast<const osg::BlendFunc*>(blended->first.get()));
            }
        }
        else if (const osg::StateAttribute::GLModeValue blend = stateSet.getMode(GL_BLEND);
                 blend != osg::StateAttribute::INHERIT && SurfaceLocks::takes(locks.mBlend, blend))
            material.mAlphaMode = (blend & osg::StateAttribute::ON) ? AlphaMode::Blend
                : material.mAlphaTest.cuts()                        ? AlphaMode::Cutout
                                                                    : AlphaMode::Opaque;

        // Only ever off by the content: a stencil property drawing both faces, or a material file's
        // two-sided flag. The scene root turns it on for everything under it.
        if (const osg::StateAttribute::GLModeValue cull = stateSet.getMode(GL_CULL_FACE);
            cull != osg::StateAttribute::INHERIT && SurfaceLocks::takes(locks.mCull, cull))
            material.mTwoSided = (cull & osg::StateAttribute::ON) == 0;

        // What `NifOsg::AlphaController` animates. `MWRender::TransparencyUpdater` writes the same
        // name beside `actorFade` to fade a whole actor, which is not the surface's own opacity and
        // is read by the walk as a fade instead.
        if (const osg::StateSet::RefUniformPair* animated = uniformNamed(stateSet, "alpha"))
            if (uniformNamed(stateSet, "actorFade") == nullptr && SurfaceLocks::takes(locks.mAlpha, animated->second))
                animated->first->get(material.mOpacity);

        // What `NifOsg` sets white under a `NiTextureEffect` and `SceneUtil::GlowUpdater` sets to
        // the enchantment's colour. Read on its own lock rather than the texture's, because the
        // glow updater rewrites the texture every frame and the colour once.
        if (const osg::StateSet::RefUniformPair* tint = uniformNamed(stateSet, "envMapColor"))
            if (SurfaceLocks::takes(locks.mEnvironmentColour, tint->second))
            {
                osg::Vec4f colour;
                if (tint->first->get(colour))
                    material.mEnvironmentColour = stated(colour);
            }

        // The ambient the game overrides for a magic effect — `SceneUtil::configureSunAmbientOverride`.
        if (const osg::StateSet::RefUniformPair* ambient = uniformNamed(stateSet, "sun.ambient"))
            if (SurfaceLocks::takes(locks.mAmbientOverride, ambient->second))
            {
                osg::Vec4f colour;
                if (ambient->first->get(colour))
                    material.mAmbientOverride = stated(colour);
            }

        readTransform(stateSet, diffuseUnit.value_or(0), material, locks);

        // **Whatever else the state set states**, every attribute and mode, each carried, of the
        // raster pipeline, or unread.
        for (const auto& [type, attribute] : stateSet.getAttributeList())
            readAttribute(*attribute.first, material);
        for (const osg::StateSet::AttributeList& unit : units)
            for (const auto& [type, attribute] : unit)
                readAttribute(*attribute.first, material);
        for (const auto& [mode, value] : stateSet.getModeList())
            readMode(mode, material);

        return said;
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
