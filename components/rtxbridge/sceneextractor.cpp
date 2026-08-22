#include "sceneextractor.hpp"

#include "lightbuilder.hpp"

#include <osg/BlendFunc>
#include <osg/CullFace>
#include <osg/Geometry>
#include <osg/Image>
#include <osg/Material>
#include <osg/NodeVisitor>
#include <osg/Texture2D>
#include <osg/TriangleIndexFunctor>
#include <osgParticle/Particle>
#include <osgParticle/ParticleSystem>

#include <array>
#include <cassert>
#include <functional>
#include <span>

#include <components/sceneutil/lightmanager.hpp>
#include <components/sceneutil/morphgeometry.hpp>
#include <components/sceneutil/riggeometry.hpp>
#include <components/sceneutil/texturetype.hpp>
// `terraindrawable.hpp` holds `osg::ref_ptr`s to composite-map types it only forward-declares, so it
// does not compile on its own. This is what completes them.
#include <components/terrain/compositemaprenderer.hpp>
#include <components/terrain/terraindrawable.hpp>

namespace RtxBridge
{
    namespace
    {
        /// Collects triangle indices whatever primitive mode the geometry used.
        ///
        /// Strips, fans and quads all arrive here as triangles, which is the only form an
        /// acceleration structure takes. Degenerate triangles — how a strip restarts — are dropped:
        /// they contribute no surface and a zero-area triangle in a BLAS is wasted traversal.
        struct TriangleCollector
        {
            std::vector<std::uint32_t>* mIndices = nullptr;

            void operator()(unsigned int a, unsigned int b, unsigned int c) const
            {
                if (a == b || b == c || a == c)
                    return;

                mIndices->push_back(a);
                mIndices->push_back(b);
                mIndices->push_back(c);
            }
        };

        /// The state set nearest the drawable, or null when nothing on the path has one.
        ///
        /// This is the material's identity. Two drawables that share it share their shading: OpenMW's
        /// optimizer collapses equivalent state sets into one object, so sharing the pointer means
        /// sharing the values, and what the parents above contribute in this graph is light and
        /// render-bin state rather than material.
        const osg::StateSet* findOwnStateSet(const osg::NodePath& path)
        {
            for (auto it = path.rbegin(); it != path.rend(); ++it)
                if (const osg::StateSet* stateSet = (*it)->getStateSet())
                    return stateSet;

            return nullptr;
        }

        /// The state set nearest the drawable that binds any texture.
        ///
        /// Separate from `findOwnStateSet` because the two can differ: `NifOsg` puts a model's
        /// textures on the geometry, but a drawable can carry a state set of its own that only sets
        /// culling or blending. Taking the whole material from whichever state set happened to hold
        /// the textures would then hand it a parent's two-sidedness.
        const osg::StateSet* findTexturedStateSet(const osg::NodePath& path)
        {
            for (auto it = path.rbegin(); it != path.rend(); ++it)
                if (const osg::StateSet* stateSet = (*it)->getStateSet())
                    if (!stateSet->getTextureAttributeList().empty())
                        return stateSet;

            return nullptr;
        }

        /// The texture bound at `unit`, or null.
        const osg::Texture2D* getTexture(const osg::StateSet& stateSet, unsigned int unit)
        {
            return dynamic_cast<const osg::Texture2D*>(
                stateSet.getTextureAttribute(unit, osg::StateAttribute::TEXTURE));
        }

        /// The role OpenMW's shader visitor assigned to `unit` — "diffuseMap", "normalMap" and the
        /// rest — or empty when it did not run or did not recognise the slot.
        std::string_view getTextureRole(const osg::StateSet& stateSet, unsigned int unit)
        {
            const osg::StateAttribute* attribute
                = stateSet.getTextureAttribute(unit, SceneUtil::TextureType::AttributeType);
            if (attribute == nullptr)
                return {};

            return attribute->getName();
        }

        bool isBlended(const osg::StateSet& stateSet)
        {
            return (stateSet.getMode(GL_BLEND) & osg::StateAttribute::ON) != 0
                && stateSet.getAttribute(osg::StateAttribute::BLENDFUNC) != nullptr;
        }

        /// Whether back faces are drawn.
        ///
        /// OpenGL culls nothing unless told to, and `NifOsg` adds a `CullFace` only where the model's
        /// stencil property asked for it — so the absence of the attribute means two-sided, not
        /// one-sided. Getting this backwards lights every sheet in the game from one side only.
        bool isTwoSided(const osg::StateSet& stateSet)
        {
            const osg::StateAttribute* attribute = stateSet.getAttribute(osg::StateAttribute::CULLFACE);
            if (attribute == nullptr)
                return true;

            return (stateSet.getMode(GL_CULL_FACE) & osg::StateAttribute::ON) == 0;
        }

        /// The image's format, as a name a person can act on.
        std::string describeFormat(const osg::Image& image)
        {
            std::string name;
            switch (image.getPixelFormat())
            {
                // One entry for both spellings: whether the file's header claimed alpha decides
                // nothing, since a BC1 block carries its punch-through bit either way.
                case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
                case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
                    name = "BC1 (DXT1)";
                    break;
                case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
                    name = "BC2 (DXT3)";
                    break;
                case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
                    name = "BC3 (DXT5)";
                    break;
                case GL_RGB:
                    name = "RGB8";
                    break;
                case GL_RGBA:
                    name = "RGBA8";
                    break;
                case GL_LUMINANCE:
                    name = "L8";
                    break;
                case GL_LUMINANCE_ALPHA:
                    name = "LA8";
                    break;
                default:
                    name = "pixel format " + std::to_string(image.getPixelFormat());
                    break;
            }

            return name + (image.getNumMipmapLevels() > 1 ? ", with mips" : ", one level");
        }

        /// A pass's texture matrix for `unit`, as the `uv * xy + zw` the shader wants.
        ///
        /// OpenSceneGraph hands the matrix to GLSL transposed — it stores rows where GLSL reads
        /// columns — so what a shader multiplies its coordinate by is the transpose of what is here,
        /// and the translation it picks up is this matrix's last row.
        osg::Vec4f getTextureTransform(const osg::StateSet& stateSet, unsigned int unit)
        {
            // Terrain binds two units and no more, so the names are spelled rather than built.
            static constexpr std::array<std::string_view, 2> sNames{ "texMat0", "texMat1" };
            assert(unit < sNames.size());

            const osg::Uniform* uniform = stateSet.getUniform(std::string(sNames[unit]));
            osg::Matrixf matrix;
            if (uniform == nullptr || !uniform->get(matrix))
                return osg::Vec4f(1.0f, 1.0f, 0.0f, 0.0f);

            return osg::Vec4f(matrix(0, 0), matrix(1, 1), matrix(3, 0), matrix(3, 1));
        }

        /// The weights of one blend map, as floats in row order.
        ///
        /// `ESMTerrain` builds these as one byte per texel in `GL_ALPHA`, which is a hundred bytes
        /// for a chunk; widening them costs a few kilobytes a cell and saves requiring 8-bit storage
        /// of the device for the sake of it.
        void readMask(const osg::Image& image, std::vector<float>& weights)
        {
            weights.clear();
            weights.reserve(static_cast<std::size_t>(image.s()) * image.t());
            for (int row = 0; row < image.t(); ++row)
                for (int column = 0; column < image.s(); ++column)
                    weights.push_back(image.getColor(column, row).a());
        }

        /// How much a material's emissive colour is worth, which a model can scale.
        ///
        /// `NifOsg` only attaches the uniform where the model asked for something other than one, so
        /// its absence is the default rather than a missing value.
        float getEmissiveMult(const osg::StateSet& stateSet)
        {
            const osg::Uniform* uniform = stateSet.getUniform("emissiveMult");
            float value = 1.0f;
            if (uniform != nullptr && uniform->get(value))
                return value;

            return 1.0f;
        }

        float getAlphaRef(const osg::StateSet& stateSet)
        {
            // The shader visitor turns the fixed-function alpha test into this uniform and removes
            // the attribute, so the uniform is the surviving record of a cutout.
            const osg::Uniform* uniform = stateSet.getUniform("alphaRef");
            float value = 0.0f;
            if (uniform != nullptr && uniform->get(value))
                return value;

            return 0.0f;
        }
    }

    ExtractionStats& ExtractionStats::operator+=(const ExtractionStats& other)
    {
        mLights += other.mLights;
        for (const auto& [format, count] : other.mTextureFormats)
            mTextureFormats[format] += count;

        mMeshesAdded += other.mMeshesAdded;
        mMeshesReused += other.mMeshesReused;
        mMaterialsAdded += other.mMaterialsAdded;
        mMaterialsReused += other.mMaterialsReused;
        mInstances += other.mInstances;
        mDeformed += other.mDeformed;
        mEmitters += other.mEmitters;
        mSprites += other.mSprites;
        mSkippedUnknown += other.mSkippedUnknown;
        mSkippedEmpty += other.mSkippedEmpty;
        return *this;
    }

    namespace
    {
        /// Walks the graph and hands every geometry it meets to the extractor.
        class ExtractionVisitor : public osg::NodeVisitor
        {
        public:
            ExtractionVisitor(
                SceneExtractor& extractor, const osg::Matrixf& root, std::size_t frame, ExtractionStats& stats)
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
                , mExtractor(extractor)
                , mRoot(root)
                , mFrame(frame)
                , mStats(stats)
            {
            }

            /// **The only node type this looks at rather than through.** Everything else it wants is
            /// a drawable; a light is a node with no geometry, so nothing below would ever see it.
            void apply(osg::Node& node) override
            {
                if (auto* source = dynamic_cast<SceneUtil::LightSource*>(&node))
                    mExtractor.addLight(*source, getNodePath(), placed(), mFrame, mStats);

                traverse(node);
            }

            /// **Accumulated on the way down rather than recomputed on the way up.**
            ///
            /// `osg::computeLocalToWorld` walks a drawable's whole path back to the root and
            /// multiplies the chain again, so a product every sibling under a transform shares is
            /// rebuilt once per sibling — O(depth) per drawable, in a visitor already standing at
            /// that depth. One multiply per transform *entered* is the same answer for a fraction
            /// of the work, and on a nine-by-nine exterior it is the difference between 47,828
            /// chain walks a frame and about a tenth as many matrix multiplies.
            ///
            /// `computeLocalToWorldMatrix` is what `computeLocalToWorld` calls on each transform it
            /// meets, and it is called here with the same null visitor, so nothing about the answer
            /// changes: an absolute reference frame still replaces the accumulation instead of
            /// adding to it, because that is the branch inside it that does so.
            void apply(osg::Transform& node) override
            {
                const osg::Matrix above = mHere;
                node.computeLocalToWorldMatrix(mHere, nullptr);

                apply(static_cast<osg::Node&>(node));

                mHere = above;
            }

            void apply(osg::Drawable& drawable) override
            {
                mExtractor.addDrawable(drawable, getNodePath(), placed(), mStats);
            }

        private:
            /// Where the node being visited stands in the world.
            ///
            /// Narrowed to single precision here and not before: `mHere` accumulates in the width
            /// `computeLocalToWorld` returned, so a placement lands on the bits it landed on when
            /// every drawable worked the chain out for itself.
            osg::Matrixf placed() const { return osg::Matrixf(mHere) * mRoot; }

            SceneExtractor& mExtractor;
            const osg::Matrixf mRoot;
            const std::size_t mFrame;
            ExtractionStats& mStats;

            /// The local-to-world of the node being visited, above `mRoot`.
            osg::Matrix mHere;
        };
    }

    ExtractionStats SceneExtractor::extract(const osg::Node& node, const osg::Matrixf& transform, std::size_t frame)
    {
        ExtractionStats stats;
        ExtractionVisitor visitor(*this, transform, frame, stats);

        // OSG's visitor API is non-const even for a visitor that only reads, which this one does.
        const_cast<osg::Node&>(node).accept(visitor);

        return stats;
    }

    std::size_t SceneExtractor::identify(const osg::NodePath& path)
    {
        std::size_t key = 0xcbf29ce484222325ull;
        for (const osg::Node* node : path)
        {
            key ^= std::hash<const osg::Node*>{}(node);
            key *= 0x100000001b3ull;
        }

        return key;
    }

    void SceneExtractor::advance()
    {
        // Swapped rather than copied, and the new one cleared: this frame's placements are next
        // frame's history, and anything that was here last frame and is not here now has gone.
        mStood.swap(mStanding);
        mStanding.clear();
    }

    namespace
    {
        /// Drops every entry not stamped with `epoch`, and collects what is left.
        ///
        /// The survivors go out unsorted and, for the emitter textures, with repeats — several
        /// candles share one sprite. `Rtx::SceneDesc::retain` takes them that way.
        template <class Map>
        std::uint32_t sweep(Map& known, std::uint64_t epoch, std::vector<Rtx::Index>& live)
        {
            live.clear();
            live.reserve(known.size());

            std::uint32_t dropped = 0;
            for (auto entry = known.begin(); entry != known.end();)
            {
                if (entry->second.mEpoch == epoch)
                {
                    live.push_back(entry->second.mIndex);
                    ++entry;
                    continue;
                }

                entry = known.erase(entry);
                ++dropped;
            }

            return dropped;
        }

        /// Carries every surviving entry's index through the compaction.
        template <class Map>
        void carry(Map& known, const std::vector<Rtx::Index>& remap)
        {
            for (auto& [key, entry] : known)
            {
                assert(entry.mIndex < remap.size());
                assert(remap[entry.mIndex] != Rtx::sNoIndex && "a kept entry was dropped anyway");
                entry.mIndex = remap[entry.mIndex];
            }
        }
    }

    Retirement SceneExtractor::retire()
    {
        Retirement went;
        went.mMeshes = sweep(mMeshes, mEpoch, mLiveMeshes);
        went.mMaterials = sweep(mMaterials, mEpoch, mLiveMaterials);
        sweep(mEmitterTextures, mEpoch, mLiveTextures);

        const std::size_t before = mScene.getTextures().size();
        if (mScene.retain(mLiveMeshes, mLiveMaterials, mLiveTextures, mRemap))
        {
            carry(mMeshes, mRemap.mMeshes);
            carry(mMaterials, mRemap.mMaterials);
            carry(mEmitterTextures, mRemap.mTextures);

            went.mTextures = static_cast<std::uint32_t>(before - mScene.getTextures().size());
        }

        // **After the sweep and not before it**, so that the walk which fills the next epoch is the
        // one this is measured against. Every entry that survived is still carrying the old stamp
        // and would be dropped on the spot otherwise.
        ++mEpoch;

        // The placements went with the compaction, and so has whatever they stood at last frame: a
        // motion vector against a scene that no longer exists is a smear, not a history.
        if (!went.empty())
        {
            mStanding.clear();
            mStood.clear();
        }

        return went;
    }

    void SceneExtractor::addLight(const SceneUtil::LightSource& source, const osg::NodePath& path,
        const osg::Matrixf& place, std::size_t frame, ExtractionStats& stats)
    {
        // A source the game has switched off contributes nothing and is not a light.
        if (source.getEmpty())
            return;

        // **The buffer update has just finished writing.** A `LightSource` is double buffered
        // because the draw thread may be reading the other one, and the mirror runs between the two
        // — after `updateTraversal` and before `renderingTraversals` — so the frame the game is
        // about to draw is the one to read.
        const SceneUtil::Light* light = const_cast<SceneUtil::LightSource&>(source).getLight(frame);
        if (light == nullptr)
            return;

        const osg::Vec4f colour = light->getDiffuse();

        const std::optional<Rtx::Light> made
            = makeLight(osg::Vec3f(colour.r(), colour.g(), colour.b()), source.getRadius(), place.getTrans());
        if (!made.has_value())
            return;

        mScene.addLight(*made);
        ++stats.mLights;
    }

    namespace
    {
        /// The geometry a drawable holds, and whether its vertices are recomputed every frame.
        struct DrawableGeometry
        {
            const osg::Geometry* mGeometry = nullptr;
            bool mDeforming = false;
        };

        /// What of a drawable there is to mirror.
        ///
        /// Nearly everything in a cell is an `osg::Geometry` and answers in one virtual call. A
        /// skinned body and a morphed face are not: each is an `osg::Drawable` keeping two internal
        /// geometries and writing the pose the last cull traversal computed into whichever of them
        /// was not being drawn. So the geometry to read is behind an accessor, it is one frame
        /// behind anything running before cull, and it is a different object every other frame.
        DrawableGeometry readDrawable(const osg::Drawable& drawable)
        {
            if (const osg::Geometry* geometry = drawable.asGeometry())
                return DrawableGeometry{ .mGeometry = geometry, .mDeforming = false };

            if (const auto* rig = dynamic_cast<const SceneUtil::RigGeometry*>(&drawable))
                return DrawableGeometry{ .mGeometry = rig->getDeformedGeometry(), .mDeforming = true };

            if (const auto* morph = dynamic_cast<const SceneUtil::MorphGeometry*>(&drawable))
                return DrawableGeometry{ .mGeometry = morph->getDeformedGeometry(), .mDeforming = true };

            return DrawableGeometry{};
        }

        /// A geometry's per-vertex positions and normals.
        struct VertexArrays
        {
            std::span<const osg::Vec3f> mPositions;

            /// Empty where the geometry has none, or binds one per primitive rather than per vertex.
            /// A per-vertex array is the only binding worth carrying: anything coarser describes the
            /// whole drawable, and a ray tracer shading a hit point wants a value it can interpolate.
            std::span<const osg::Vec3f> mNormals;
        };

        VertexArrays readVertices(const osg::Geometry& geometry)
        {
            VertexArrays arrays;

            const auto* positions = dynamic_cast<const osg::Vec3Array*>(geometry.getVertexArray());
            if (positions == nullptr)
                return arrays;

            arrays.mPositions = std::span(positions->asVector());

            const auto* normals = dynamic_cast<const osg::Vec3Array*>(geometry.getNormalArray());
            if (normals != nullptr && normals->size() == positions->size())
                arrays.mNormals = std::span(normals->asVector());

            return arrays;
        }
    }

    void SceneExtractor::addDrawable(
        const osg::Drawable& drawable, const osg::NodePath& path, const osg::Matrixf& place, ExtractionStats& stats)
    {
        // Asked before the geometry, because a particle system is an `osg::Drawable` with no
        // triangles in it at all: its sprites *are* the drawing, and they leave here as a run of
        // discs rather than as a mesh anything could build a structure over.
        if (const auto* particles = dynamic_cast<const osgParticle::ParticleSystem*>(&drawable))
        {
            addEmitter(*particles, path, place, stats);
            return;
        }

        const DrawableGeometry read = readDrawable(drawable);
        if (read.mGeometry == nullptr)
        {
            ++stats.mSkippedUnknown;
            return;
        }

        const osg::Geometry& geometry = *read.mGeometry;
        const Rtx::Index mesh = resolveMesh(drawable, geometry, read.mDeforming, stats);
        if (mesh == Rtx::sNoIndex)
            return;

        // Terrain keeps its material on the drawable rather than on the graph, so it is asked first
        // and the state-set walk never sees a chunk.
        const auto* terrain = dynamic_cast<const Terrain::TerrainDrawable*>(&geometry);
        const Rtx::Index material
            = terrain != nullptr ? resolveTerrainMaterial(*terrain, stats) : resolveMaterial(path, stats);

        // Where it stood last frame, or here if nothing saw it move — a placement met for the first
        // time has no history, and saying it moved from nowhere is worse than saying it stood still.
        const std::size_t who = identify(path);
        const auto stood = mStood.find(who);
        mStanding.emplace(who, place);

        mScene.addInstance(Rtx::MeshInstance{
            .mTransform = place,
            .mPrevious = stood != mStood.end() ? std::optional<osg::Matrixf>(stood->second) : std::nullopt,
            .mMesh = mesh,
            .mMaterial = material,
        });
        ++stats.mInstances;
    }

    namespace
    {
        /// Whether the nearest pass on the path adds to the frame rather than covering it.
        ///
        /// **The nearest state set that has a blend function, not simply the nearest one.** A
        /// particle system carries a state set of its own that sets neither blending nor texture —
        /// `NifOsg` puts both on the transform above it — so asking the drawable's own would answer
        /// "covers" for every flame in the game.
        ///
        /// The split is the whole of what tells a flame from a puff of smoke, and 474 of the game's
        /// 678 emitters are on the adding side. One that blends over is an albedo and has to be lit;
        /// one that adds *is* light and must not be.
        bool addsLight(const osg::NodePath& path)
        {
            for (auto it = path.rbegin(); it != path.rend(); ++it)
            {
                const osg::StateSet* stateSet = (*it)->getStateSet();
                if (stateSet == nullptr)
                    continue;

                const auto* blend
                    = dynamic_cast<const osg::BlendFunc*>(stateSet->getAttribute(osg::StateAttribute::BLENDFUNC));
                if (blend == nullptr)
                    continue;

                return blend->getSource() == osg::BlendFunc::SRC_ALPHA
                    && blend->getDestination() == osg::BlendFunc::ONE;
            }

            return false;
        }

        /// The uniform scale a placement carries, as the length of its first basis row.
        ///
        /// A sprite's size is in the particle system's own coordinates — `LOCAL_COORDINATES` is what
        /// `NifOsg` sets — so the quad the rasterizer would draw is scaled by the modelview along
        /// with everything else. Morrowind scales references uniformly, so one number says it.
        float scaleOf(const osg::Matrixf& place)
        {
            return osg::Vec3f(place(0, 0), place(0, 1), place(0, 2)).length();
        }
    }

    void SceneExtractor::addEmitter(const osgParticle::ParticleSystem& particles, const osg::NodePath& path,
        const osg::Matrixf& place, ExtractionStats& stats)
    {
        // A particle's whole silhouette is its texture's alpha, so an emitter with no texture has
        // nothing to draw — not a white disc, which is what sampling nothing would give it.
        const osg::StateSet* textured = findTexturedStateSet(path);
        if (textured == nullptr)
            return;

        const osg::Texture2D* sprite = getTexture(*textured, 0);
        if (sprite == nullptr || sprite->getImage(0) == nullptr || sprite->getImage(0)->getFileName().empty())
            return;

        // **Registered the first time the emitter is seen, and not the first time it has a
        // particle alive.** The texture array is uploaded when the scene is built; a flame that was
        // empty at load and lights up two hundred frames later would otherwise add a texture on a
        // frame that only re-places what is already there, and index past the array it is sampling.
        //
        // Kept in a map of its own because nothing else can speak for it when the scene is swept: a
        // sprite's texture is on no material, and an emitter is not in the scene between frames.
        auto [known, arrived] = mEmitterTextures.try_emplace(&particles);
        if (arrived)
            known->second.mIndex = mScene.addTexture(VFS::Path::Normalized(sprite->getImage(0)->getFileName()));

        known->second.mEpoch = mEpoch;
        const Rtx::Index texture = known->second.mIndex;

        const float scale = scaleOf(place);

        mSpriteScratch.clear();
        const int held = particles.numParticles();
        for (int at = 0; at < held; ++at)
        {
            const osgParticle::Particle* particle = particles.getParticle(at);

            // A dead slot keeps its last position and is waiting to be born again. Drawing one is a
            // spark frozen where the previous one expired.
            if (!particle->isAlive())
                continue;

            const float radius = particle->getCurrentSize() * scale;
            if (!(radius > 0.0f))
                continue;

            // `getCurrentColor`'s alpha and `getCurrentAlpha` are two separate ramps and the
            // rasterizer multiplies them. OpenMW's `ParticleColorAffector` writes the record's
            // colour ramp into the first with its alpha forced to one and the alpha into the
            // second, so in this content the product is the second — and multiplying both is what
            // keeps that a fact about the data rather than an assumption in the reader.
            const osg::Vec4f colour = particle->getCurrentColor();
            const float alpha = colour.a() * particle->getCurrentAlpha();
            if (!(alpha > 0.0f))
                continue;

            mSpriteScratch.push_back(Rtx::Sprite{
                .mPosition = particle->getPosition() * place,
                .mRadius = radius,
                .mColour = osg::Vec3f(colour.r(), colour.g(), colour.b()),
                .mAlpha = alpha,
            });
        }

        if (mSpriteScratch.empty())
            return;

        ++stats.mTextureFormats[describeFormat(*sprite->getImage(0))];

        mScene.addEmitter(mSpriteScratch, texture, addsLight(path));

        ++stats.mEmitters;
        stats.mSprites += static_cast<std::uint32_t>(mSpriteScratch.size());
    }

    Rtx::Index SceneExtractor::resolveTerrainMaterial(const Terrain::TerrainDrawable& terrain, ExtractionStats& stats)
    {
        const Terrain::TerrainDrawable::PassVector& passes = terrain.getPasses();
        if (passes.empty())
            return Rtx::sNoIndex;

        // The first pass is as good an identity as the chunk itself and is already a state set, so
        // terrain shares the material map with everything else.
        const osg::StateSet* identity = passes.front().get();
        const auto known = mMaterials.find(identity);
        if (known != mMaterials.end())
        {
            ++stats.mMaterialsReused;
            known->second.mEpoch = mEpoch;
            return known->second.mIndex;
        }

        Rtx::Material material;
        material.mKind = Rtx::MaterialKind::Terrain;
        material.mLayerOffset = static_cast<Rtx::Index>(mScene.getLayers().size());

        for (const osg::ref_ptr<osg::StateSet>& pass : passes)
        {
            const osg::Texture2D* diffuse = getTexture(*pass, 0);
            if (diffuse == nullptr || diffuse->getImage(0) == nullptr || diffuse->getImage(0)->getFileName().empty())
                continue;

            Rtx::MaterialLayer layer;
            layer.mDiffuse = mScene.addTexture(VFS::Path::Normalized(diffuse->getImage(0)->getFileName()));
            layer.mDiffuseTransform = getTextureTransform(*pass, 0);
            ++stats.mTextureFormats[describeFormat(*diffuse->getImage(0))];

            // A chunk of a single ground type is given no blend map at all, and stays at full weight.
            const osg::Texture2D* mask = getTexture(*pass, 1);
            if (mask != nullptr && mask->getImage(0) != nullptr)
            {
                const osg::Image& image = *mask->getImage(0);
                readMask(image, mMaskScratch);
                layer.mMaskOffset = mScene.addMask(mMaskScratch);
                layer.mMaskWidth = static_cast<std::uint16_t>(image.s());
                layer.mMaskHeight = static_cast<std::uint16_t>(image.t());
                layer.mMaskTransform = getTextureTransform(*pass, 1);
            }

            mScene.addLayer(layer);
            ++material.mLayerCount;
        }

        if (material.mLayerCount == 0)
            return Rtx::sNoIndex;

        const Rtx::Index index = mScene.addMaterial(material);
        mMaterials.emplace(identity, Known{ .mIndex = index, .mEpoch = mEpoch });
        ++stats.mMaterialsAdded;
        return index;
    }

    Rtx::Index SceneExtractor::resolveMesh(
        const osg::Drawable& drawable, const osg::Geometry& geometry, bool deforming, ExtractionStats& stats)
    {
        const auto known = mMeshes.find(&drawable);
        if (known != mMeshes.end())
        {
            ++stats.mMeshesReused;
            known->second.mEpoch = mEpoch;

            // Nothing else in the map is re-read: the whole point of it is that a crate met again is
            // the crate already uploaded. A pose is not, so this is the one path that goes back to
            // the vertex arrays on a hit — and it is why the mirror stays cheap for a cell and pays
            // only for what is actually moving.
            if (deforming)
            {
                const VertexArrays arrays = readVertices(geometry);
                mScene.updateMesh(known->second.mIndex, arrays.mPositions, arrays.mNormals);
                ++stats.mDeformed;
            }

            return known->second.mIndex;
        }

        const VertexArrays arrays = readVertices(geometry);
        if (arrays.mPositions.empty())
        {
            ++stats.mSkippedEmpty;
            return Rtx::sNoIndex;
        }

        mIndexScratch.clear();
        osg::TriangleIndexFunctor<TriangleCollector> collector;
        collector.mIndices = &mIndexScratch;
        geometry.accept(collector);

        if (mIndexScratch.empty())
        {
            ++stats.mSkippedEmpty;
            return Rtx::sNoIndex;
        }

        std::span<const osg::Vec2f> texCoords;
        const auto* texCoordArray = dynamic_cast<const osg::Vec2Array*>(geometry.getTexCoordArray(0));
        if (texCoordArray != nullptr && texCoordArray->size() == arrays.mPositions.size())
            texCoords = std::span(texCoordArray->asVector());

        const Rtx::Index mesh = mScene.addMesh(arrays.mPositions, arrays.mNormals, texCoords, mIndexScratch);
        mMeshes.emplace(&drawable, Known{ .mIndex = mesh, .mEpoch = mEpoch });
        ++stats.mMeshesAdded;
        if (deforming)
            ++stats.mDeformed;

        return mesh;
    }

    Rtx::Index SceneExtractor::resolveMaterial(const osg::NodePath& path, ExtractionStats& stats)
    {
        const osg::StateSet* own = findOwnStateSet(path);
        if (own == nullptr)
            return Rtx::sNoIndex;

        const auto known = mMaterials.find(own);
        if (known != mMaterials.end())
        {
            ++stats.mMaterialsReused;
            known->second.mEpoch = mEpoch;
            return known->second.mIndex;
        }

        Rtx::Material material;

        if (const osg::StateSet* textured = findTexturedStateSet(path))
        {
            for (unsigned int unit = 0; unit < textured->getTextureAttributeList().size(); ++unit)
            {
                const osg::Texture2D* texture = getTexture(*textured, unit);
                if (texture == nullptr || texture->getImage(0) == nullptr)
                    continue;

                const std::string& file = texture->getImage(0)->getFileName();
                if (file.empty())
                    continue;

                const Rtx::Index index = mScene.addTexture(VFS::Path::Normalized(file));
                ++stats.mTextureFormats[describeFormat(*texture->getImage(0))];
                const std::string_view role = getTextureRole(*textured, unit);

                // Unit 0 without a role is the diffuse slot: that is where `NifOsg` puts the base map
                // when the shader visitor has not run to label it.
                if (role == "diffuseMap" || (role.empty() && unit == 0))
                    material.mDiffuse = index;
                else if (role == "normalMap" || role == "normalHeightMap")
                    material.mNormal = index;
                else if (role == "emissiveMap")
                    material.mEmissive = index;
            }
        }

        material.mAlphaRef = getAlphaRef(*own);
        if (isBlended(*own))
            material.mAlphaMode = Rtx::AlphaMode::Blend;
        else if (material.mAlphaRef > 0.0f)
            material.mAlphaMode = Rtx::AlphaMode::Cutout;

        material.mTwoSided = isTwoSided(*own);

        const auto* colours = dynamic_cast<const osg::Material*>(own->getAttribute(osg::StateAttribute::MATERIAL));
        if (colours != nullptr)
        {
            material.mDiffuseColour = colours->getDiffuse(osg::Material::FRONT);

            // Folded together here because the game's own shader only ever uses their product.
            const osg::Vec4f emissive = colours->getEmission(osg::Material::FRONT) * getEmissiveMult(*own);
            material.mEmissiveColour = osg::Vec3f(emissive.x(), emissive.y(), emissive.z());
        }

        const Rtx::Index index = mScene.addMaterial(material);
        mMaterials.emplace(own, Known{ .mIndex = index, .mEpoch = mEpoch });
        ++stats.mMaterialsAdded;
        return index;
    }
}
