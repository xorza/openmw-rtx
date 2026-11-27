#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>

#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include <components/rtx/common/index.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/image/alpharange.hpp>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/scene.h>

#include "surface.hpp"

namespace Rtx
{
    /// A row of the device's material tables: the scene's materials from `Shaders::MATERIAL_ROW_FIRST` on, past the
    /// sentinel rows before them (`materialRecordOf`).
    using MaterialRecordIndex = RowIndex<struct MaterialRecordRows>;

    /// The device's row of `material`.
    constexpr MaterialRecordIndex materialRecordOf(const MaterialIndex material)
    {
        return MaterialRecordIndex(material.at() + Shaders::MATERIAL_ROW_FIRST);
    }

    /// What shading a hit takes, which is not a variation on one path but three different ones.
    enum class MaterialKind
    {
        /// One diffuse texture over a lit surface, which is nearly everything in the game.
        Surface,

        /// A stack of tiling ground textures, each masked by its own grid of weights.
        Terrain,

        /// Water, which has no albedo at all: it reflects, refracts and absorbs, and its colour is
        /// what is behind and above it rather than anything of its own.
        Water,
    };

    /// Which hits on a terrain chunk read its stack flattened into one texture, its composite, and
    /// not the stack itself.
    enum class GroundFlattening
    {
        /// None: a chunk of one layer is one fetch already, and wants no composite.
        None,

        /// The hits no picture is drawn from, a diffuse bounce's far end: a chunk inside the active
        /// grid, whose eye, reflections and bed read the stack for its sharpness. At a bounce's far
        /// hit the stack's detail is averaged away, and the composite read there took 0.05 to 0.08 ms
        /// off the trace at 1080p and 0.2 to 0.6 ms at 7680 by 2160 (`./omw bench`).
        Undetailed,

        /// Every hit: a chunk outside the active grid, where the quad tree flattens too.
        Every,
    };

    /// What traversal is told about one placement: the facts of the material it wears, with the
    /// placement's own fade applied (`Material::Traversed::placedAt`). The one rule, which the
    /// counts that gate the trace and the instance records that drive traversal both read, so the
    /// two cannot disagree about a placement.
    struct PlacedTraversal
    {
        /// Whether traversal stops to ask whether a hit is a hole, where it would not stop for the
        /// placement anyway: a translucent one is stopped for and never ends the ray.
        bool mCutout = false;

        /// Whether traversal stops to ask how much of a hit there is. Earned by the material, for a
        /// pane, or by the placement, for an actor the game is fading — and never by a surface that
        /// adds, whose alpha weights what it adds rather than deciding how much of it is there.
        bool mTranslucent = false;

        bool mMedium = false;
        bool mAdditive = false;
    };

    /// How a surface is shaded, as the file says it. Vanilla textures are pre-lit, so `mDiffuse` is
    /// not an albedo yet.
    ///
    /// **The normal and specular maps are the content's companions, found by name**: every one
    /// arrives as an `_n`, `_nh` or `_spec` file beside the diffuse, which `Shader::MapVisitor`
    /// attaches at load under the same `[Shaders]` switches the rasterizer reads. A surface that has
    /// none is vanilla, and vanilla pictures do not change: that is the rule these maps enter under.
    struct Material
    {
        MaterialKind mKind = MaterialKind::Surface;

        TextureIndex mDiffuse = sNoRow;

        /// A map of what glows, read at the texture unit the content bound it at, as the dark map
        /// below is: the draugrs' eyes read the geometry's second set.
        TextureIndex mEmissive = sNoRow;
        std::uint8_t mEmissiveUnit = 0;

        /// A sphere-mapped sheet the surface adds past its albedo, indexed by where the eye is —
        /// a `NiTextureEffect`, or the caustic sheet an enchanted item shimmers with — and what it
        /// is tinted by, in linear light. `sNoRow` for the surfaces that carry none, which is
        /// nearly all of them.
        TextureIndex mEnvironment = sNoRow;
        osg::Vec3f mEnvironmentColour{ 1.0f, 1.0f, 1.0f };

        /// A map the albedo is multiplied by, read at the texture unit the content bound it at,
        /// because half the vanilla dark maps read the geometry's second set of coordinates and
        /// which set a unit reads is the mesh's to say — `GpuMesh::mUnitStreams`.
        TextureIndex mDark = sNoRow;
        std::uint8_t mDarkUnit = 0;

        /// The companion maps: a tangent-space normal in the normal encoding, and a metal-roughness
        /// map, a classic one converted to it (`SurfaceDescription::encodingOf`). `sNoRow` where the
        /// content has none.
        TextureIndex mNormal = sNoRow;
        TextureIndex mSpecular = sNoRow;

        /// The two maps `objects.frag` lays into the colour past the dark map, each at the unit the content
        /// bound it at: a detail map the albedo is multiplied by twice over, so its grey leaves it as it was,
        /// and a decal laid over the albedo by its own alpha. No shipped file binds either; a mod's surfaces do.
        TextureIndex mDetail = sNoRow;
        std::uint8_t mDetailUnit = 0;
        TextureIndex mDecal = sNoRow;
        std::uint8_t mDecalUnit = 0;

        /// The two maps that change the environment map, kept only where there is one, since the rasterizer
        /// reads them nowhere else: a bump map that moves where the sheet is read by its red and green through
        /// `mBumpMatrix` (its four entries row by row) and dims it by its blue through `mBumpLuma`
        /// (`clamp(b * x + y)`), and a gloss map the sheet is multiplied by.
        TextureIndex mBump = sNoRow;
        std::uint8_t mBumpUnit = 0;
        osg::Vec4f mBumpMatrix{ 0.0f, 0.0f, 0.0f, 0.0f };
        osg::Vec2f mBumpLuma{ 0.0f, 0.0f };
        TextureIndex mGloss = sNoRow;
        std::uint8_t mGlossUnit = 0;

        /// Where the normal map's alpha is a height the texture coordinates are shifted by toward the
        /// eye, as the rasterizer's `parallax.glsl` shifts them (`Shaders::MATERIAL_PARALLAX`), the
        /// heights it spans (`alphaRange`): what bounds the march through its relief. Nothing where the
        /// alpha is no height.
        ///
        /// **Never on a cutout**, which the rasterizer does not spare: a hole is found by the
        /// traversal's test, which has no eye to shift toward, and a surface that shifted where it
        /// shades and not where it is cut would put the leaf and the hole in different places.
        std::optional<AlphaRange> mRelief{};

        /// What the texture is tinted by, in linear light. Three channels and not the record's
        /// four: the alpha beside it is `mOpacity` and is not a colour.
        osg::Vec3f mDiffuseColour{ 1.0f, 1.0f, 1.0f };

        /// What the texture is tinted by under the fill, in linear light: the record's ambient
        /// colour, which the rasterizer lights by the cell's ambient as it lights `mDiffuseColour` by
        /// the lamps and the sun — `texture × (D × lit + A × ambient + E)`. Under it the trace's fill
        /// and the ambient at a path's end; a bounce of a lamp's light stays the diffuse's. Nothing
        /// where nothing stated one: its diffuse stands for it, `getAmbientColour`.
        std::optional<osg::Vec3f> mAmbientColour{};

        /// What the fill is reflected by: the stated ambient colour, or the diffuse where none is.
        osg::Vec3f getAmbientColour() const { return mAmbientColour.value_or(mDiffuseColour); }

        /// Whether the fill reflects off this surface otherwise than the bounce does: an ambient
        /// colour apart from the diffuse, or a vertex colour that replaces one of the two and not the
        /// other. Where neither, the trace forms the two albedos by one expression over one input, so
        /// they are one number to the bit and the fill adds nought to the bounce (`composedLight`).
        bool hasAmbientApart() const
        {
            return getAmbientColour() != mDiffuseColour || mVertexColour == VertexColour::Diffuse
                || mVertexColour == VertexColour::Ambient;
        }

        /// How much the surface glows on its own, with the material's own multiplier folded in,
        /// because the game's own shader only ever uses their product.
        osg::Vec3f mEmissiveColour{ 0.0f, 0.0f, 0.0f };

        /// How much of the surface is there, as the content stated it and before its texture is
        /// read. One for everything that is all there, which is nearly everything.
        float mOpacity = 1.0f;

        AlphaTest mAlphaTest{};

        AlphaMode mAlphaMode = AlphaMode::Opaque;

        /// How a blended surface composites: over what is behind it, or added to it. Meaningful
        /// under `AlphaMode::Blend`, and what tells a magic effect's sheet from a pane of glass.
        BlendKind mBlend = BlendKind::Over;

        /// What this surface's per-vertex colour is for — the tint that replaces `mDiffuseColour`,
        /// the glow that replaces `mEmissiveColour`, or nothing. On the material and not on the
        /// mesh, because a `NiVertexColorProperty` hangs above a shape and two shapes sharing one
        /// state set share the mode. What a mesh carries is the colours themselves.
        VertexColour mVertexColour = VertexColour::None;

        /// Both faces of this surface are drawn, which the content says by turning `GL_CULL_FACE`
        /// off. Vanilla says it by doubling a shape instead, and `InstanceRecord::mTwoSided` is
        /// where the two ways of saying it meet and a ray that draws reads them.
        bool mTwoSided = false;

        /// Whether the lamps light this surface — `SurfaceDescription::mLampLit`.
        bool mLampLit = true;

        /// Whether this surface is groundcover's — `SurfaceDescription::mGroundcover`.
        bool mGroundcover = false;

        /// Mesh texture coordinates to this material's, as `uv * xy + zw` — the same form the
        /// terrain layers use. Morrowind moves lava, waterfalls, banners and smoke by rewriting a
        /// texture matrix every frame — 432 surfaces in Vivec alone — so this is on the material,
        /// which `setMaterial` rewrites, and not on the instance.
        osg::Vec4f mTextureTransform{ 1.0f, 1.0f, 0.0f, 0.0f };

        /// Where this material's terrain layers sit in the scene's layer table. Empty for everything
        /// that is not terrain, which is all but a handful of materials in a cell, so the layered
        /// path costs the rest of them one comparison and no indirection.
        Run mLayers{};

        /// Which hits read this chunk's stack flattened into one texture. Asked for here and
        /// answered later, because a composite costs tens of milliseconds; until one arrives
        /// `mDiffuse` stays unset and every hit shades from the stack, so only the cost per hit
        /// differs.
        GroundFlattening mFlatten = GroundFlattening::None;

        /// Whether any of this chunk's layers reads a map: a normal map, or an authored albedo with
        /// its roughness beside it (`Shaders::LAYER_AUTHORED`). A layer run is the scene's and not
        /// the row's, so the placer that fills the run says so here, where `Traversed::mMapped`
        /// can read it.
        bool mLayersMapped = false;

        /// Whether a controller rewrites this material's state set, or one above it, every frame.
        /// Constant for the material's whole life, because `SceneExtractor::animate` gives a node
        /// with a controller a state set of its own, and a material is keyed by every state set of
        /// its chain (`ChainKeys`). It is what refuses such a material to the replay, which reuses
        /// what it read last frame.
        bool mAnimated = false;

        /// Whether the diffuse map's alpha never reaches solid anywhere on it — `ImageFacts` —
        /// which is what makes a blend a pane rather than a mask (`isTranslucent`), and a pane a
        /// cloud (`isMedium`). False for a material with no diffuse map at all, which is an
        /// untextured pane.
        bool mDiffuseNeverSolid = false;

        /// What one texel of the diffuse map adds on average under this material's blend, in
        /// linear light: weighted by its own alpha where the blend reads one and whole where it
        /// does not — `ImageFacts::mMean`. Read for an additive material and nothing else, because what
        /// asks is a magic effect's glow, and left at the untextured white for one with no map,
        /// which is what its sheets are drawn with. Nought for a map nothing here can decode.
        osg::Vec3f mDiffuseMean = Shaders::UNTEXTURED_ALBEDO;

        /// For telling a rewrite from a no-op: a state set with a controller on it is re-read every
        /// frame and usually says exactly what it said last time.
        bool operator==(const Material& other) const = default;

        /// The test a texel is a hole by, or one that passes every alpha where the surface has
        /// none. A blend is cut at `Shaders::ALPHA_PANE_CUTOFF` where it is a pane and at
        /// `Shaders::ALPHA_BLEND_CUTOFF` where it is a mask, because that is where the game keeps its
        /// foliage — or at the content's own test where that cuts more (`foldsTest`), and at the
        /// content's own test alone where it cuts otherwise. **On the sides the content's test passes**
        /// where the two fold: a soft edge's floor is that test's reference (`getSoftEdgeFloor`), and a
        /// texel standing on it under `GREATER` is one the content drops.
        AlphaTest getAlphaTest() const
        {
            switch (mAlphaMode)
            {
                case AlphaMode::Opaque:
                    return AlphaTest{ .mPasses = Shaders::ALPHA_PASSES_ALL };
                case AlphaMode::Cutout:
                    return mAlphaTest;
                case AlphaMode::Blend:
                {
                    const AlphaTest blends{ .mReference
                        = isTranslucent() ? Shaders::ALPHA_PANE_CUTOFF : Shaders::ALPHA_BLEND_CUTOFF };
                    if (!mAlphaTest.cuts())
                        return blends;
                    if (!foldsTest())
                        return mAlphaTest;
                    return mAlphaTest.mReference >= blends.mReference
                        ? mAlphaTest
                        : AlphaTest{ .mReference = blends.mReference, .mPasses = mAlphaTest.mPasses };
                }
            }
            return AlphaTest{ .mPasses = Shaders::ALPHA_PASSES_ALL };
        }

        /// The least alpha the eye meets of a soft edge (`isSoftEdged`), whose cut is
        /// `Shaders::ALPHA_BLEND_CUTOFF`: where its blend draws nothing, or what the content's own test
        /// drops, whichever is more.
        float getSoftEdgeFloor() const
        {
            assert(isSoftEdged());
            return mAlphaTest.cuts() ? std::max(mAlphaTest.mReference, Shaders::ALPHA_PANE_CUTOFF)
                                     : Shaders::ALPHA_PANE_CUTOFF;
        }

        /// Whether the content's own test drops only what stands under its reference (`GREATER`,
        /// `GEQUAL`), the kind of cut a blend makes of its own, so the two are one test: the higher.
        bool foldsTest() const
        {
            return (mAlphaTest.mPasses & Shaders::ALPHA_PASSES_BELOW) == 0u
                && (mAlphaTest.mPasses & Shaders::ALPHA_PASSES_ABOVE) != 0u;
        }

        /// Every texture slot this material names in its own right, each once, whichever of them
        /// is set.
        ///
        /// **Beside the fields, because the list and the fields fall out of step nowhere else.**
        /// What keeps a slot alive is the material row that names it, so a map missing from this
        /// list is a slot freed under a live material and handed to the next texture that arrives.
        /// A map added to the struct is added here, and `MaterialTable` adds the layers' own to what
        /// this walks.
        template <class Visit>
        void forEachTexture(Visit visit) const
        {
            visit(mDiffuse);
            visit(mEmissive);
            visit(mEnvironment);
            visit(mDark);
            visit(mNormal);
            visit(mSpecular);
            visit(mDetail);
            visit(mDecal);
            visit(mBump);
            visit(mGloss);
        }

        /// Whether traversal has to stop and ask this material whether a hit is a hole — the one
        /// predicate the build marks an instance non-opaque by and the shader tests against. A
        /// cutoff with no texture to sample is not one, and neither is an additive surface, whose
        /// alpha weights what it adds rather than deciding whether it is there.
        bool isCutout() const { return getAlphaTest().cuts() && mDiffuse != sNoRow && !isAdditive(); }

        /// Whether this surface adds to what is behind it and covers nothing — `BlendKind::Add`
        /// or `AddWhole` under a blend. Such a surface is no pane and no mask: it is gathered by
        /// `additiveAlong` on a mask of its own and met by no other ray.
        bool isAdditive() const { return additiveSurface(mAlphaMode, mBlend); }

        /// Whether this is a terrain chunk that asked to be flattened and has no composite yet.
        bool wantsFlattening() const
        {
            return mKind == MaterialKind::Terrain && mFlatten != GroundFlattening::None && mDiffuse == sNoRow;
        }

        /// Whether `mOpacity` is a number the trace reads at all: what the content asked a blend to
        /// weigh by, which is coverage where the surface covers and strength where it adds. An
        /// opaque or cutout surface is all there, and what its alpha holds is not a coverage.
        bool isBlended() const { return mAlphaMode == AlphaMode::Blend; }

        /// Whether it wears a detail, a decal, a bump or a gloss map, a mod's maps beside the diffuse
        /// (`Shaders::MATERIAL_MOD_MAPS`).
        bool wearsModMaps() const
        {
            return mDetail != sNoRow || mDecal != sNoRow || mBump != sNoRow || mGloss != sNoRow;
        }

        /// Whether what is behind this surface is meant to show through it, by the rasterizer's own
        /// rule: a blend draws every texel at its alpha (`objects.frag`) and discards only where the
        /// content asked for a test. The two answers want opposite things from traversal — a mask
        /// averaged and tested is right for the leaf, light attenuated as it passes for the pane.
        /// Not the opposite of `isCutout`, and a pane is both.
        ///
        /// **A blend is traced as a cut where its texture is a mask.** Morrowind keeps its foliage
        /// under `NiAlphaProperty` with no test, so a leaf card and a pane of glass carry the same
        /// mode. A mask is solid wherever its paint is, so a cut at a half differs from the blend
        /// along its fringe alone, and a cut is what traversal resolves cheaply. A texture that
        /// never reaches solid is no mask: the lantern's glass peaks at 119, and a cut drops all of
        /// it. So a material alpha below one, or such a texture, is a pane. An additive surface is
        /// neither: it covers nothing.
        ///
        /// **A test of the content's own only takes texels away**: `objects.frag` discards what fails
        /// it and blends the rest at its alpha, so a pane or a mask stays one under it, and the test
        /// raises the cut where it stands over the blend's (`getAlphaTest`). A mod's glow card blends
        /// a texture that never closes under a test of alpha over nought, and cut there it was a
        /// solid sheet glowing whole.
        ///
        /// **A mask's soft fringe stays a cut for every ray but the eye's** (`isSoftEdged`): traced
        /// as panes, the soft edges of every DXT3 leaf, banner and rope turned the trace's sun and
        /// sky rays through them to grain.
        bool isTranslucent() const
        {
            return mAlphaMode == AlphaMode::Blend && mBlend == BlendKind::Over
                && (mOpacity < 1.0f || mDiffuseNeverSolid);
        }

        /// Whether this is a blended mask: a blend over what is behind it with a material alpha of one,
        /// whose texture reaches solid — every leaf card, banner, rope and sail the game ships, and a
        /// cobweb or a crystal that is solid anywhere at all. Cut at `Shaders::ALPHA_BLEND_CUTOFF` for
        /// every ray but the eye's: **the eye meets a texel the cut drops with a chance of its alpha**
        /// (`Shaders::MATERIAL_SOFT_EDGE`), down to `getSoftEdgeFloor`, so the frames average to the
        /// blend the rasterizer draws; what it meets is the solid it is. A light ray keeps the cut,
        /// which is noiseless and cheap. Under a test of the content's own that drops more than the
        /// cut, or that keeps what is under it, there is no fringe to draw, and the test is the cut.
        bool isSoftEdged() const
        {
            return mAlphaMode == AlphaMode::Blend && mBlend == BlendKind::Over && mOpacity >= 1.0f
                && !mDiffuseNeverSolid && mDiffuse != sNoRow
                && (!mAlphaTest.cuts() || (foldsTest() && mAlphaTest.mReference < Shaders::ALPHA_BLEND_CUTOFF));
        }

        /// Whether the eye passes through this rather than meeting it: a medium, not a surface.
        /// Two facts and neither alone — the material's own alpha below one, which a leaf's does not
        /// say and a lantern's glass does not either, and a texture whose paint never closes, which
        /// a pane's lead came does. Where both hold the layers are composited as depth along the
        /// ray — `mediumAlong`.
        bool isMedium() const { return isTranslucent() && mOpacity < 1.0f && mDiffuseNeverSolid; }

        /// What a placement keeps of the material it wears — what its row tells traversal, and what
        /// the scene's counts read — stated once because the record builder writes it into the row
        /// and `setMaterial` has to know whether a rewrite changed it.
        struct Traversed
        {
            MaterialKind mKind = MaterialKind::Surface;
            bool mCutout = false;
            bool mTranslucent = false;

            /// Whether the placements wearing this material go into the structure under
            /// `MASK_MEDIUM` as well, which is the one ray that gathers them.
            bool mMedium = false;

            /// Whether they go in under `MASK_ADDITIVE` and nothing else: seen by the one query
            /// that gathers what adds, and by no ray that shades, shadows or bounces.
            bool mAdditive = false;

            /// What `Material::mTwoSided` says, carried here because the row that culls by it is
            /// the placement's and not the material's.
            bool mTwoSided = false;

            /// Whether the material wears a normal map or a specular map: what the trace compiles
            /// the maps' code in for, and only for a scene that places such a material
            /// (`InstanceCounts::mMapped`).
            bool mMapped = false;

            /// Whether the material wears a detail, a decal, a bump or a gloss map (`wearsModMaps`), which a scene's
            /// count reports (`InstanceCounts::mModMapped`).
            bool mModMapped = false;

            /// Whether its relief spans more than one height, which a march has to find its way
            /// through: what the trace compiles the march in for, and only for a scene that places
            /// such a material (`InstanceCounts::mRelief`). A flat map's relief is its shift.
            bool mRelief = false;

            bool operator==(const Traversed& other) const = default;

            /// What a placement wearing this at `opacity` tells traversal.
            PlacedTraversal placedAt(const float opacity) const
            {
                const bool translucent = !mAdditive && (opacity < 1.0f || mTranslucent);
                return PlacedTraversal{
                    .mCutout = mCutout && !translucent,
                    .mTranslucent = translucent,
                    .mMedium = mMedium,
                    .mAdditive = mAdditive,
                };
            }
        };

        Traversed getTraversed() const
        {
            return Traversed{
                .mKind = mKind,
                .mCutout = isCutout(),
                .mTranslucent = isTranslucent(),
                .mMedium = isMedium(),
                .mAdditive = isAdditive(),
                .mTwoSided = mTwoSided,
                .mMapped = mNormal != sNoRow || mSpecular != sNoRow || mLayersMapped,
                .mModMapped = wearsModMaps(),
                .mRelief = mRelief.has_value() && mRelief->spans(),
            };
        }
    };

    /// One layer of a terrain material: a ground texture and the weights that place it. The
    /// device's own row, carried whole from the land record to the scene's layer run and the
    /// bake — `Shaders::GpuLayer` says what each field is. A fresh one is all nought, which is a
    /// layer with no texture and no transform: `wholeLayer` is what a reader starts from.
    using MaterialLayer = Shaders::GpuLayer;

    /// The weights a layer placed, as the run the mask table handed out — what is given back is
    /// what was taken, because the count is the grid's area. Here and not on the row, because the
    /// row is a shared header's and `Run` is the host's.
    inline Run maskOf(const MaterialLayer& layer)
    {
        return Run{ .mOffset = layer.mMaskOffset, .mCount = layer.mMaskWidth * layer.mMaskHeight };
    }

    /// A layer that covers the whole chunk at its cell coordinates and names no texture yet: no
    /// grid, and both transforms the identity `uv * (1, 1) + (0, 0)`. What every layer starts as
    /// before the land record says otherwise, because a value-initialised row would place its
    /// texture at one texel.
    inline MaterialLayer wholeLayer()
    {
        return MaterialLayer{
            .mDiffuse = Shaders::NO_TEXTURE,
            .mMaskOffset = 0,
            .mMaskWidth = 0,
            .mMaskHeight = 0,
            .mDiffuseTransform = osg::Vec4f(1.0f, 1.0f, 0.0f, 0.0f),
            .mMaskTransform = osg::Vec4f(1.0f, 1.0f, 0.0f, 0.0f),
            .mNormal = Shaders::NO_TEXTURE,
            .mFlags = 0,
        };
    }
}
