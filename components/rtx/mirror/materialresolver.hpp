#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <osg/Node>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <components/rtx/common/runs.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/preprocess/meantexels.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/rowhold.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/specularlayout.hpp>
#include <components/rtx/scene/surface.hpp>

#include "mirroridentity.hpp"
#include "mirrorpass.hpp"

namespace osg
{
    class Image;
    class NodeVisitor;
    class StateSet;
}

namespace SceneUtil
{
    class StateSetUpdater;
}

namespace Rtx
{
    class ContentPreprocessor;
    struct Shading;

    /// What a chain of state sets says a surface is, read where the chain is and adopted where the
    /// scene is. Everything here points into the state sets it was read from, so a reading is good
    /// for as long as the model that carries them stands.
    struct MaterialReading
    {
        /// The state set the material is held under: the nearest one to the drawable. Null where
        /// the chain was empty, which is a drawable that wears nothing.
        const osg::StateSet* mKey = nullptr;

        /// What the content said, or nothing where nothing did.
        std::optional<SurfaceDescription> mDescribed{};

        /// Whether the diffuse map's alpha ever reaches solid — decided by the reader for the one
        /// kind of surface the answer changes, a translucent one, and left unset for every other.
        /// The reader answers it because the walk over the texels is the reading's whole cost.
        std::optional<bool> mDiffuseSolid{};

        /// What a texel of the diffuse map adds on average, `Material::mDiffuseMean` — decided by
        /// the reader for an additive surface, for the same reason, and left unset for every other.
        std::optional<osg::Vec3f> mDiffuseMean{};
    };

    /// Turns what the content says a surface is into the scene's materials, and keeps the textures
    /// they name. Keyed on the state set, which OpenMW's optimizer makes a meaningful identity; a
    /// controller rewriting one is the exception, and `resolve` reads that one again on every
    /// frame. The animation is here because OpenMW animates shading with a state set that belongs
    /// to the traversal rather than to the graph, so a walk has to build it.
    class MaterialResolver
    {
    public:
        /// A material slot and the state set it is held under, because which state set of a chain
        /// names a material is this class's answer.
        struct Resolved
        {
            Index mIndex = sNoIndex;
            const osg::StateSet* mKey = nullptr;
        };

        /// @param pass the walk in progress: its sweep stamp and its counts, read at every call.
        ///        Borrowed, so that the mirror and everything resolving into it cannot come to hold
        ///        two answers.
        /// @param means the thread's mean texels, shared with the emitters for the reason
        ///        `EmitterResolver` gives.
        /// @param content the walk's thread's, which reads a translucent map's alpha.
        MaterialResolver(SceneDesc& scene, const MirrorPass& pass, MeanTexels& means, ContentPreprocessor& content)
            : mScene(scene)
            , mPass(pass)
            , mMeans(means)
            , mContent(content)
        {
        }

        /// Gives back every hold an entry of the maps took, so a scene that outlives this holds
        /// nothing of it. The scene outlives it by the reference this keeps.
        ~MaterialResolver();

        MaterialResolver(const MaterialResolver&) = delete;
        MaterialResolver& operator=(const MaterialResolver&) = delete;

        /// The material slot for the chain of state sets in force at a drawable.
        Resolved resolve(std::span<const Shading> shading);

        /// Reads the chain of state sets in force at a drawable, for a thread that has no scene to
        /// resolve into. `resolve` is the same reading followed by `adopt`.
        ///
        /// @param content that thread's own, which reads a translucent diffuse map's alpha.
        /// @param means that thread's own cache of additive maps' means.
        static MaterialReading read(std::span<const Shading> shading, ContentPreprocessor& content, MeanTexels& means);

        /// The material slot for a reading, adding it where the mirror holds none under its key,
        /// with one hold taken on the entry — `MeshResolver::adopt` says why a hold. Standing only:
        /// a reading carries no controller. `sNoIndex` and no hold for a reading with no key.
        Index adopt(const MaterialReading& reading);

        /// Gives one `adopt` back, by the state set the reading named. Nothing for null.
        void release(const osg::StateSet* key);

        /// The sea's own, keyed on the state set it has not got because a node mask is what
        /// identifies it.
        Resolved resolveWater();

        /// What the `_spec` maps of materials described from here on mean, `Ignore` until told.
        void setSpecularLayout(SpecularLayout layout) { mSpecularLayout = layout; }

        /// The state set `node` shades with where that is not simply the one it wears, or null
        /// where it is — which is nearly every node in a cell. One per node, rewritten in place, so
        /// a material keyed on its address is the same material next frame.
        ///
        /// **Two nodes need one: the node a controller writes, and a node standing under one.** For
        /// the first it is what the controller wrote. For the second it is an identity: what a
        /// material is keyed on has to be unique to the placement, and the state set the content
        /// gave a shape is shared by every instance of the model. `SceneUtil::addEnchantedGlow`
        /// hangs its sheet on an instance's root, above the shape the sheet is read into — so keyed
        /// on the shared state set, one material stands for the enchanted sword and the plain one
        /// beside it at once, is read once, and never cycles its sheet.
        ///
        /// @param underAnimated whether an animated state set is above `node` on the chain —
        ///        `Shading::mAnimatedThrough`.
        const osg::StateSet* animate(osg::Node& node, osg::NodeVisitor* visitor, bool underAnimated);

        /// Drops every entry neither this epoch nor a hold keeps, and with it the entry's hold on
        /// its material and on every image it wore.
        void retire();

        /// Lets go of the images and the animated state sets this epoch did not meet. Asked
        /// whatever the materials did, because a cached material's images go stale on the frame
        /// after they arrived.
        void retireHolds();

        /// Reserves the identity maps once, so no frame rehashes them. `SceneExtractor` states the
        /// budgets.
        void reserve(std::size_t materials, std::size_t textures, std::size_t animated)
        {
            mMaterials.reserve(materials);
            mTextureOf.reserve(textures);
            mAnimated.reserve(animated);
        }

    private:
        /// What the scene knows one image as under each encoding and wrap, and whether its alpha
        /// ever reaches solid, unset until something asks, because the walk over its texels is only
        /// worth doing for a material that has to tell a wisp from a mask. No one index: the slots
        /// are twelve, and the sweep reads the reach alone.
        struct HeldTexture
        {
            Reach mReach;

            /// The entry's hold on each slot it took, one per wrap and encoding, and empty where it
            /// took none.
            std::array<std::array<TextureHold, sTextureWrapCount>, sTextureEncodingCount> mSlots;
            std::optional<bool> mSolid;

            /// Which of `mSlots` the table refused, a bit each. An animated material asks for its
            /// images every frame, and a full table refused each of them every frame, building the
            /// path to be refused. Per slot and not per image, because another wrap of the same
            /// file may stand already, and the table answers that one full or not.
            RefusedTakes mRefused;
            static_assert(sTextureEncodingCount * sTextureWrapCount <= 16, "a refusal bit per slot");

            /// Its mean texel in the thread's cache, `MeanTexels`, or null until an additive
            /// material asks.
            const MeanTexel* mMean = nullptr;
        };

        /// Every image an animated material has worn, each held in `mTextureOf` for as long as the
        /// material stands.
        ///
        /// **`SceneUtil::GlowUpdater` cycles thirty-two caustic sheets at sixteen a second**, one
        /// `setTextureAttribute` a frame, and a resolver that kept only the sheet of the frame gave
        /// one back and took one up on every frame: an `extendScene` a frame with a decode and an
        /// upload in it. Thirty-two for the cycle, because that is the largest the game ships, and
        /// a place for each of the material's other maps beside it: the ring holds every image the
        /// material wears, and a ring of the cycle alone holds the diffuse map too, so every sheet
        /// change drops the sheet due next and takes it up again a sixteenth of a second later.
        /// One past what fits drops the oldest and counts it.
        struct Worn
        {
            static constexpr std::size_t sCycle = 32;
            static constexpr std::size_t sMost = sCycle + sSurfaceMapCount - 1;

            std::array<const osg::Image*, sMost> mImages{};
            std::uint8_t mCount = 0;
            std::uint8_t mNext = 0;
        };

        /// A material, the entry's hold on it, and, where a controller rewrites it, what it has
        /// worn.
        struct HeldMaterial
        {
            MaterialHold mRow;
            Reach mReach{};
            std::optional<Worn> mWorn{};
        };

        /// How many controllers one node's chains are applied from: an actor's root carries a glow
        /// and a fade at the most, and a controller past these is left unapplied.
        static constexpr std::size_t sMostUpdaters = 4;

        /// The state set a node's controllers write into, kept so that the address a material is
        /// keyed on is the same one next frame. See `animate`. An entry like any other, so the map
        /// sweeps it by the reach every entry carries.
        struct Animated
        {
            Reach mReach;

            osg::ref_ptr<osg::StateSet> mStateSet;

            /// The controllers found on the node's callback chains, in the order the rasterizer runs
            /// them, and what the chains looked like when they were found. None where the node is
            /// animated by an ancestor alone.
            std::array<SceneUtil::StateSetUpdater*, sMostUpdaters> mUpdaters{};
            std::size_t mUpdaterCount = 0;
            bool mSetUp = false;
            std::uintptr_t mChains = 0;
        };

        /// Reads a whole material off the chain, which is what an arrival and a rewrite both want.
        Material readMaterial(std::span<const Shading> shading, Worn* worn);

        /// The material a reading comes to, with its images taken into the scene. What the reader
        /// answered about the diffuse map is taken as read, and what it left unset is asked of the
        /// image here, only where it matters.
        ///
        /// @param worn what an animated material keeps of every image it has worn, or null for
        ///        one nothing rewrites.
        Material describe(const MaterialReading& reading, bool animated, Worn* worn);

        using Entry = Identity<const osg::StateSet, HeldMaterial>::Entry;

        /// The entry `key` already holds, stamped and counted as a reuse, or the map's end. The
        /// one spelling of meeting a known material, whichever of the three ways in met it.
        Entry reuse(const osg::StateSet* key);

        /// Adds `material` under `key`, counted as an arrival.
        Entry adopt(const osg::StateSet* key, const Material& material);

        /// Gives back every hold `worn` took on the images it names.
        void releaseWorn(const Worn& worn);

        /// Gives back what one entry holds on the scene: a material's row and what it wore, an
        /// image's slots.
        void release(HeldMaterial& held);
        void release(HeldTexture& held);

        /// The scene's slot for one image under one wrap and one encoding, held for as long as this
        /// names it.
        ///
        /// @param worn the material wearing it, where that material is rewritten by a controller,
        ///        which keeps the texture through the frames the controller shows another one.
        Index takeTexture(const TextureUse& use, Worn* worn, TextureEncoding encoding = TextureEncoding::Colour);

        /// Whether `image`'s alpha ever reaches solid — `reachesSolid`, read at the first material
        /// that asks and kept. Asked only for a translucent material's own diffuse map, because it
        /// walks every texel of the finest level.
        bool diffuseReachesSolid(const osg::Image* image);

        /// What a texel of `image` adds on average under `blend` — `MeanTexels::of`, found by the
        /// slot after the first ask. Asked only for an additive material's own diffuse map, because
        /// the first ask for a file walks its texels. The untextured grey for no image.
        osg::Vec3f diffuseMeanOf(const osg::Image* image, BlendKind blend);

        SceneDesc& mScene;
        const MirrorPass& mPass;

        /// Which state set each material came from, and the sea under the one it has not got —
        /// `resolveWater`. Owning, so that a state set cannot go while the entry stands: see
        /// `ByAddress`.
        Identity<const osg::StateSet, HeldMaterial> mMaterials{ mPass };

        /// Which slot each image the walk has met stands in, so an animated material re-read every
        /// frame does not build four `VFS::Path::Normalized` strings a frame. The entry is a
        /// reference, because a slot whose last material stops naming it is handed out again at
        /// once.
        Identity<const osg::Image, HeldTexture> mTextureOf{ mPass };

        /// Owning for the same reason the identity maps are: a node freed and replaced at the same
        /// address would otherwise be handed the state set the first one's controllers were writing.
        Identity<const osg::Node, Animated> mAnimated{ mPass };

        /// The extractor's, both. The ring's reader has its own and hands its answers over in the
        /// reading.
        MeanTexels& mMeans;
        ContentPreprocessor& mContent;

        SpecularLayout mSpecularLayout = SpecularLayout::Ignore;
    };
}
