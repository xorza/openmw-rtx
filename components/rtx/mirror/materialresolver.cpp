#include "materialresolver.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include <osg/Callback>
#include <osg/CopyOp>
#include <osg/Image>
#include <osg/StateSet>
#include <osg/Vec2f>
#include <osg/Vec4f>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/image/colour.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/preprocess/imagefactcache.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/surface.hpp>
#include <components/rtx/shaders/look.h>
#include <components/sceneutil/statesetupdater.hpp>
#include <components/vfs/pathutil.hpp>

#include "extractionstats.hpp"
#include "shading.hpp"

namespace Rtx
{
    namespace
    {
        /// What the sea's material is keyed on: the state set it has not got (`resolveWater`).
        /// Nothing else in the world can key as null, because a shading chain's entries come from
        /// `SceneExtractor::Traversal::pushShading`, which takes a reference.
        constexpr const osg::StateSet* sSea = nullptr;

        /// What one texel of a sheet adds on average under `blend`: weighted by its own alpha
        /// where the blend reads one, and whole where `AddWhole` reads none.
        osg::Vec3f meanUnder(const MeanTexel& mean, const BlendKind blend)
        {
            return blend == BlendKind::AddWhole ? mean.mWhole : mean.mColour;
        }

        /// Every state-set controller on `node`'s two chains, into `into`, in the order the
        /// rasterizer runs them: the update traversal's chain before the cull traversal's, so a fade
        /// applied at cull lands over a glow applied at update. `NifOsg` hangs anything marked
        /// `AnimFlag_AutoPlay` from a cull callback and everything else from an update callback.
        std::size_t findUpdaters(osg::Node& node, std::span<SceneUtil::StateSetUpdater*> into)
        {
            std::size_t count = 0;
            for (osg::Callback* chain : { node.getUpdateCallback(), node.getCullCallback() })
                for (osg::Callback* callback = chain; callback != nullptr; callback = callback->getNestedCallback())
                    if (auto* updater = dynamic_cast<SceneUtil::StateSetUpdater*>(callback);
                        updater != nullptr && count < into.size())
                        into[count++] = updater;

            return count;
        }

        /// What hangs on `node`'s two chains, as one number: a callback added, removed or swapped
        /// anywhere on either changes it. Pointer arithmetic down chains of one or two, against the
        /// casts `findUpdaters` takes.
        std::uintptr_t chainSignature(const osg::Node& node)
        {
            std::uintptr_t signature = 0;
            for (const osg::Callback* chain : { node.getCullCallback(), node.getUpdateCallback() })
                for (const osg::Callback* callback = chain; callback != nullptr;
                     callback = callback->getNestedCallback())
                    signature = (signature * 31u) ^ reinterpret_cast<std::uintptr_t>(callback);

            return signature;
        }
    }

    const osg::StateSet* MaterialResolver::animate(osg::Node& node, osg::NodeVisitor* visitor, const bool underAnimated)
    {
        // Asked of every node in the graph every frame, and nearly all of a cell hangs off no
        // callback at all and stands under nothing animated.
        const bool chained = node.getCullCallback() != nullptr || node.getUpdateCallback() != nullptr;
        const bool inherits = underAnimated && node.getStateSet() != nullptr;
        if (!chained && !inherits)
            return nullptr;

        // The casts are taken when the chains change, not per frame. The entry remembers what it
        // found and what the chains looked like when it found it; a controller swapped, appended or
        // removed under the walk changes the signature, and a node whose chains carry no updater
        // keeps a null one.
        const auto [entry, arrived] = mAnimated.reach(&node);
        Animated& held = entry->second;
        const std::uintptr_t chains = chainSignature(node);
        if (arrived || chains != held.mChains)
        {
            held.mChains = chains;

            std::array<SceneUtil::StateSetUpdater*, sMostUpdaters> found{};
            const std::size_t count = chained ? findUpdaters(node, found) : 0;

            if (count != held.mUpdaterCount
                || !std::equal(found.begin(), found.begin() + count, held.mUpdaters.begin()))
            {
                held.mUpdaters = found;
                held.mUpdaterCount = count;
                held.mSetUp = false;
            }
        }

        const std::span<SceneUtil::StateSetUpdater* const> updaters(held.mUpdaters.data(), held.mUpdaterCount);
        if (updaters.empty() && !inherits)
            return nullptr;

        for (std::size_t at = 0; at < updaters.size(); ++at)
            held.mSetUp = held.mSetUp && updaters[at]->getGeneration() == held.mGenerations[at];

        // **Set up again whenever the controllers that apply to it change, or one asks to be**, so
        // none applies to a state set it did not set up: a fade that came after a glow read the
        // uniforms the glow's defaults never made, a fade that went left its blend and its alpha
        // behind, and a glow that ended kept its last sheet. Reset in place rather than made anew,
        // because the address is what the material table keys the surface by, and a new one would
        // be a second material for the same surface.
        if (!held.mSetUp)
        {
            held.mSetUp = true;

            if (held.mStateSet == nullptr)
                held.mStateSet = new osg::StateSet;
            held.mStateSet->clear();

            // A shallow copy of what the node already wears: `applyCull` starts from an empty state
            // set and lets the rasterizer's state stack supply the rest, so a fire would lose its
            // material along with its animation. Read rather than created, because
            // `getOrCreateStateSet` would leave an empty one on a node that had none, pushed over
            // the material a parent was contributing.
            if (const osg::StateSet* base = node.getStateSet(); base != nullptr)
            {
                held.mStateSet->merge(*base);

                // `merge` leaves the bin to the set merged into.
                held.mStateSet->setRenderingHint(base->getRenderingHint());
                held.mStateSet->setRenderBinDetails(base->getBinNumber(), base->getBinName(), base->getRenderBinMode());
                held.mStateSet->setNestRenderBins(base->getNestRenderBins());
            }
            for (SceneUtil::StateSetUpdater* updater : updaters)
                updater->setDefaults(held.mStateSet);
        }

        for (std::size_t at = 0; at < updaters.size(); ++at)
        {
            updaters[at]->apply(held.mStateSet, visitor);
            held.mGenerations[at] = updaters[at]->getGeneration();
        }
        return held.mStateSet;
    }

    MaterialResolver::Entry MaterialResolver::reuse(const osg::StateSet* const key)
    {
        const Entry known = mMaterials.find(key);
        if (known == mMaterials.end())
            return known;

        ++mPass.getStats().mMaterialsReused;
        mMaterials.stamp(known);

        return known;
    }

    MaterialResolver::Entry MaterialResolver::adopt(const osg::StateSet* const key, const Material& material)
    {
        MaterialHold row = mScene.holdMaterial(mScene.addMaterial(material));
        ++mPass.getStats().mMaterialsAdded;

        return mMaterials.add(key, HeldMaterial{ .mRow = std::move(row) });
    }

    void MaterialResolver::releaseWorn(const Worn& worn)
    {
        for (std::size_t at = 0; at < worn.mCount; ++at)
        {
            const auto held = mTextureOf.find(worn.mImages[at]);
            Crash::contract(held != mTextureOf.end(), "a worn image the mirror does not hold");
            mTextureOf.drop(held);
        }
    }

    MaterialResolver::Resolved MaterialResolver::resolveWater()
    {
        // One material for the sea, identified by the state set it has not got: water has no
        // albedo, `MWRender::Water` swaps its node's state set between two copies every frame, and
        // with `water shader = true` there is no state set on the node at all. In the map under
        // `sSea` rather than beside it, so that one sweep and one count answer for every material.
        if (const Entry held = reuse(sSea); held != mMaterials.end())
            return Resolved{ .mIndex = held->second.mRow.get(), .mKey = sSea };

        // **Drawn from both faces**, which is what `SceneUtil::createSimpleWaterStateSet` says by
        // turning `GL_CULL_FACE` off: a swimmer looks up at the surface from under it.
        return Resolved{ .mIndex
            = adopt(sSea, Material{ .mKind = MaterialKind::Water, .mTwoSided = true })->second.mRow.get(),
            .mKey = sSea };
    }

    MaterialReading MaterialResolver::read(std::span<const Shading> shading, ImageFactCache& facts)
    {
        if (shading.empty())
            return MaterialReading{};

        MaterialReading reading{ .mKey = shading.back().mStateSet };
        if (!describeSurface(shading, reading.mDescribed.emplace()))
        {
            reading.mDescribed.reset();
            return reading;
        }

        // Off the description the material is copied from, so the reader walks the texels of
        // exactly the images `describe` would.
        const SurfaceDescription& described = *reading.mDescribed;
        const osg::Image* const diffuse = described.getTexture(SurfaceMap::Diffuse);

        if (described.mAlphaMode == AlphaMode::Blend && diffuse != nullptr && !diffuse->getFileName().empty())
            reading.mDiffuseFacts = facts.of(*diffuse);

        return reading;
    }

    Index MaterialResolver::adopt(const MaterialReading& reading)
    {
        if (reading.mKey == nullptr)
            return sNoIndex;

        Entry known = reuse(reading.mKey);
        if (known == mMaterials.end())
            known = adopt(reading.mKey, describe(reading, false, nullptr));

        mMaterials.hold(known);
        return known->second.mRow.get();
    }

    void MaterialResolver::release(const osg::StateSet* const key)
    {
        if (key == nullptr)
            return;

        const auto known = mMaterials.find(key);
        Crash::contract(known != mMaterials.end(), "a material released that the mirror does not hold");
        mMaterials.drop(known);
    }

    MaterialResolver::Resolved MaterialResolver::resolve(std::span<const Shading> shading)
    {
        if (shading.empty())
            return Resolved{};

        // The material's identity is the state set nearest the drawable. Two drawables that share
        // it share their shading: OpenMW's optimizer collapses equivalent state sets into one
        // object, so sharing the pointer means sharing the values, and what the parents above
        // contribute in this graph is light and render-bin state rather than material.
        const Shading& own = shading.back();

        if (const Entry known = reuse(own.mStateSet); known != mMaterials.end())
        {
            // Read again, because a controller rewrote it since the last frame. The state set
            // is the same object — that is what lets the material keep its slot and every placement
            // standing on it stay where it is — and everything inside it is this frame's. What it
            // wears is kept across the frames, for the reason `Worn` gives.
            if (own.mAnimated)
            {
                HeldMaterial& held = known->second;
                // From a value, since Clang asks whether `Worn` is default-constructible while the
                // class it is nested in is still incomplete, and keeps the answer no.
                if (!held.mWorn.has_value())
                    held.mWorn.emplace(Worn{});
                mScene.setMaterial(held.mRow.get(), readMaterial(shading, &*held.mWorn));
            }

            return Resolved{ .mIndex = known->second.mRow.get(), .mKey = own.mStateSet };
        }

        // An arrival under a controller starts wearing what it wears from its first frame.
        if (!own.mAnimated)
            return Resolved{ .mIndex = adopt(own.mStateSet, readMaterial(shading, nullptr))->second.mRow.get(),
                .mKey = own.mStateSet };

        Worn worn;
        const Material material = readMaterial(shading, &worn);
        const Entry added = adopt(own.mStateSet, material);
        added->second.mWorn = worn;

        return Resolved{ .mIndex = added->second.mRow.get(), .mKey = own.mStateSet };
    }

    Index MaterialResolver::takeTexture(const TextureUse& use, Worn* const worn, const TextureEncoding encoding)
    {
        ExtractionStats& stats = mPass.getStats();

        const osg::Image* const image = use.get();
        if (image == nullptr || image->getFileName().empty())
            return sNoIndex;

        auto known = mTextureOf.find(image);
        if (known != mTextureOf.end())
            mTextureOf.stamp(known);
        else
            known = mTextureOf.add(image, HeldTexture{});

        HeldTexture& held = known->second;
        TextureHold& slot = held.mSlots[static_cast<std::size_t>(encoding)][static_cast<std::size_t>(use.mWrap)];
        const std::uint64_t freed = mScene.textures().getFreedCount();
        const auto bit = static_cast<std::uint16_t>(
            1u << (static_cast<std::size_t>(encoding) * sTextureWrapCount + static_cast<std::size_t>(use.mWrap)));
        if (slot.empty() && !held.mRefused.stands(bit, freed))
        {
            // Held, because this entry is the reference. `mTextureOf` says why a slot the map names
            // has to be one nothing else can hand out.
            slot = mScene.takeTexture(VFS::Path::Normalized(image->getFileName()), *image, use.mWrap, encoding);
            if (slot.empty())
                held.mRefused.refuse(bit, freed);
        }

        if (worn != nullptr)
        {
            const auto first = worn->mImages.begin();
            const auto last = first + worn->mCount;
            if (std::find(first, last, image) == last)
            {
                if (worn->mCount == Worn::sMost)
                {
                    // The oldest goes, which is the one `mNext` stands on once the ring is full.
                    const auto oldest = mTextureOf.find(worn->mImages[worn->mNext]);
                    Crash::contract(oldest != mTextureOf.end(), "a worn image the mirror does not hold");
                    mTextureOf.drop(oldest);
                    ++stats.mWornBeyondKept;
                }
                else
                    ++worn->mCount;

                worn->mImages[worn->mNext] = image;
                worn->mNext = static_cast<std::uint8_t>((worn->mNext + 1) % Worn::sMost);
                mTextureOf.hold(known);
            }
        }

        return slot.get();
    }

    const ImageFacts* MaterialResolver::diffuseFacts(const osg::Image* const image)
    {
        if (image == nullptr)
            return nullptr;

        // Asked only of an image `takeTexture` already met, which is the only way a material can
        // come to name one. Anything else is a texture this cannot answer for, and a material
        // keeps the answers that leave it traced as an untextured one would be.
        const auto known = mTextureOf.find(image);
        if (known == mTextureOf.end())
            return nullptr;

        // Kept by the slot, so the frames after the first find it without the name.
        const ImageFacts*& facts = known->second.mFacts;
        if (facts == nullptr)
            facts = &mFacts.of(*image);

        return facts;
    }

    Material MaterialResolver::readMaterial(std::span<const Shading> shading, Worn* const worn)
    {
        const bool animated = !shading.empty() && shading.back().mAnimated;

        // Nothing read ahead: the answers about the map are asked of the images here, where the
        // walk is, and kept against the next material that names them.
        MaterialReading reading;
        if (!describeSurface(shading, reading.mDescribed.emplace()))
            reading.mDescribed.reset();

        return describe(reading, animated, worn);
    }

    Material MaterialResolver::describe(const MaterialReading& reading, const bool animated, Worn* const worn)
    {
        ExtractionStats& stats = mPass.getStats();

        Material material;

        // Before the description, because a surface nothing described is still one a controller
        // rewrites: what the flag states is a fact about the state set and not about what is in it.
        material.mAnimated = animated;

        if (!reading.mDescribed.has_value())
        {
            ++stats.mUndescribedSurfaces;
            return material;
        }

        const SurfaceDescription* const described = &*reading.mDescribed;

        // Kept, because the medium test below asks about the same image and asking the description
        // twice for it is asking twice.
        const osg::Image* const diffuse = described->getTexture(SurfaceMap::Diffuse);

        material.mDiffuse = takeTexture(described->getTextureUse(SurfaceMap::Diffuse), worn);
        material.mEmissive = takeTexture(described->getTextureUse(SurfaceMap::Emissive), worn);
        material.mEnvironment = takeTexture(described->getTextureUse(SurfaceMap::Environment), worn);
        material.mEnvironmentColour = decodeColour(described->mEnvironmentColour);
        material.mDark = takeTexture(described->getTextureUse(SurfaceMap::Dark), worn);
        material.mDarkUnit = described->mDarkUnit;

        // The companion maps are data, and a specular map is read only in the layout the player
        // named: a classic one read as metalness and roughness is wrong, so none is read.
        material.mNormal = takeTexture(described->getTextureUse(SurfaceMap::Normal), worn, TextureEncoding::Normal);
        if (mSpecularLayout == SpecularLayout::MetalRoughness)
            material.mSpecular
                = takeTexture(described->getTextureUse(SurfaceMap::Specular), worn, TextureEncoding::Data);

        material.mAlphaRef = described->mAlphaRef;
        material.mAlphaMode = described->mAlphaMode;
        material.mBlend = described->mBlend;
        material.mVertexColour = described->mVertexColour;

        material.mTwoSided = described->mTwoSided;
        material.mOpacity = described->mOpacity;

        // Decoded here, because this is where the game's numbers enter the trace. A record's
        // colour is written in the space the artist saw and everything past this is light.
        material.mDiffuseColour = decodeColour(described->mDiffuseColour);

        // The multiplier is applied past the decode: it is a gain on the light and not a colour of
        // its own. Folded in because the game's own shader only ever uses their product.
        material.mEmissiveColour = decodeColour(described->mEmissiveColour) * described->mEmissiveMult;

        // The ambient the game overrides joins the glow, which is where the rasterizer's own sum
        // puts it: `objects.frag` adds `ambientColor * ambientLight` beside the emission and
        // multiplies the whole by the texture, and a white override makes that the material's
        // ambient colour whole. A lighting term and not a colour, so it takes the glow's scale.
        if (described->mAmbientOverride.has_value())
        {
            const osg::Vec3f overridden = decodeColour(*described->mAmbientOverride);
            const osg::Vec3f ambient = decodeColour(described->mAmbientColour);
            material.mEmissiveColour
                += osg::Vec3f(overridden.x() * ambient.x(), overridden.y() * ambient.y(), overridden.z() * ambient.z());
        }

        material.mParallax = described->mNormalHeight && material.mNormal != sNoIndex && !material.isCutout();

        // Scaled about the middle of the texture, then offset, which is what `NifOsg` builds its
        // texture matrix from — so `(uv - 0.5) * scale + 0.5 + offset`, resolved here into the
        // `uv * xy + zw` the sampler takes. Doing the arithmetic once on the host keeps two
        // multiplies and an add out of every texture fetch in the frame.
        const osg::Vec2f scale = described->mTextureScale;
        const osg::Vec2f offset = described->mTextureOffset;
        material.mTextureTransform = osg::Vec4f(
            scale.x(), scale.y(), 0.5f * (1.0f - scale.x()) + offset.x(), 0.5f * (1.0f - scale.y()) + offset.y());

        // Last, and only for the surfaces the answer separates. Every field the tests read is
        // filled above, and the walk over a texture's texels is worth nothing to a material that is
        // opaque, tested, or has no diffuse map to read: whether a blend is a pane or a cut, and a
        // pane a medium, is the texture's alpha, and a glow is asked of an additive sheet alone. The
        // reading's answer where one was made, and the walk over the texels only where none was.
        if (material.isBlended() && material.mDiffuse != sNoIndex)
        {
            const ImageFacts* const facts
                = reading.mDiffuseFacts.has_value() ? &*reading.mDiffuseFacts : diffuseFacts(diffuse);
            if (facts != nullptr)
            {
                material.mDiffuseNeverSolid = !facts->mReachesSolid;
                if (material.isAdditive())
                    material.mDiffuseMean = meanUnder(facts->mMean, material.mBlend);
            }
        }

        return material;
    }

    void MaterialResolver::retire()
    {
        mMaterials.retire([this](HeldMaterial& held) { release(held); });
    }

    MaterialResolver::~MaterialResolver()
    {
        // The materials first, because what one wore is a hold on an entry of the images' map.
        mMaterials.clear([this](HeldMaterial& held) { release(held); });
        mTextureOf.clear([this](HeldTexture& held) { release(held); });
    }

    void MaterialResolver::release(HeldMaterial& held)
    {
        if (held.mWorn.has_value())
            releaseWorn(*held.mWorn);
        mScene.drop(std::move(held.mRow));
    }

    void MaterialResolver::release(HeldTexture& held)
    {
        for (std::array<TextureHold, sTextureWrapCount>& slots : held.mSlots)
            for (TextureHold& slot : slots)
                mScene.drop(std::move(slot));
    }

    void MaterialResolver::retireHolds()
    {
        // The walk's own hold on every image a material is read from, given back the same way.
        // Most are met once and go stale on the frame after they arrived; what settles here is the
        // animated materials.
        mTextureOf.retire([this](HeldTexture& held) { release(held); });

        // What `animate` keeps. Swept beside everything else because it is keyed on a node the graph
        // can drop, and because a state set held past its node holds the textures in it alive too.
        mAnimated.retire();
    }
}
