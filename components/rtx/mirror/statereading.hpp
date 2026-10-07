#pragma once

#include <cstdint>
#include <string_view>

#include <osg/StateAttribute>
#include <osg/StateSet>

#include <components/rtx/mirror/surfacedescription.hpp>

namespace osg
{
    class StateSet;
}

namespace Rtx
{
    /// What a parent state set has claimed for the rest of a chain.
    ///
    /// **OpenGL resolves a chain root first, and a parent that set a value with `OVERRIDE` keeps
    /// it against every child that did not set its own `PROTECTED`.** A fold that let the child
    /// win every time lost the one texture the game overrides from above — `MWRender::overrideTexture`
    /// puts the blood's texture on an effect's root that way. One lock per thing the fold reads,
    /// carried across one fold and never further.
    struct SurfaceLocks
    {
        /// One bit per `TextureRole`.
        std::uint32_t mTextures = 0;

        bool mMaterial = false;
        bool mAlphaFunc = false;
        bool mBlend = false;
        bool mCull = false;
        bool mAlpha = false;
        bool mTextureMatrix = false;
        bool mAmbientOverride = false;
        bool mEnvironmentColour = false;

        /// Whether a value carrying `flags` is taken under `lock`, and claims the lock for the rest
        /// of the chain where it carries `OVERRIDE`. The whole of OpenGL's rule, in one place.
        static bool takes(bool& lock, osg::StateAttribute::OverrideValue flags)
        {
            if (lock && (flags & osg::StateAttribute::PROTECTED) == 0)
                return false;
            if ((flags & osg::StateAttribute::OVERRIDE) != 0)
                lock = true;
            return true;
        }

        bool takesTexture(TextureRole role, osg::StateAttribute::OverrideValue flags);
    };

    /// Folds what one state set says about a surface into `into`, and says whether it said
    /// anything at all. One state set at a time, nearest last, because that is how OpenGL resolves a
    /// chain. A texture's role is the `SceneUtil::TextureType` at its unit or the sampler uniform
    /// naming it, and a unit nothing names is not the surface's; the colours, the opacity, the
    /// glossiness and the vertex-colour mode come off the `SceneUtil::Material`, the opacity again
    /// off an `alpha` uniform unless an `actorFade` stands beside it, the alpha test off the
    /// `osg::AlphaFunc` or the `alphaRef` uniform the visitor moved it into, blending off a
    /// `BlendFunc` or `GL_BLEND` and how it composites off the `BlendFunc`'s factors, two-sidedness
    /// off `GL_CULL_FACE`, the texture transform off the `texMat<unit>` uniform on the diffuse
    /// unit, the environment map's tint off `envMapColor`, and the ambient the game overrides off
    /// `sun.ambient`.
    ///
    /// A parent's `OVERRIDE` is honoured through `locks`, which a caller folding a chain carries
    /// from the root down and a caller reading one state set leaves at its default.
    ///
    /// @return whether the state set carried a material or a texture: what tells a surface from a
    ///         node that only sets a mode or a uniform on the way down.
    bool describeStateSet(const osg::StateSet& stateSet, SurfaceDescription& into, SurfaceLocks& locks);

    /// One state set on its own, under no lock.
    bool describeStateSet(const osg::StateSet& stateSet, SurfaceDescription& into);

    /// Whether `stateSet` holds anything `describeStateSet` reads into a description: an attribute
    /// or a mode it carries or reports unread, a texture, or a uniform. One that holds only the
    /// raster pipeline's — a depth test, a program, a winding — describes nothing, and a material
    /// key passes it by (`ChainKeys`).
    bool describesAnything(const osg::StateSet& stateSet);

    /// The uniform `stateSet` holds under `name` and the flags it was set with, or null. Compared
    /// along the list rather than looked up, because the list's `std::map` takes only a
    /// `std::string` key: a name built for every state set of every drawable's chain, every frame,
    /// where a list holds a handful of uniforms and nearly every one holds none.
    const osg::StateSet::RefUniformPair* findUniform(const osg::StateSet& stateSet, std::string_view name);
}
