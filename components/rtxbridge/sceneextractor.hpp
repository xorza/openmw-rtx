#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <osg/Matrixf>
#include <osg/Node>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/rtx/scenedesc.hpp>

namespace osg
{
    class Geometry;
    class StateSet;
}

namespace SceneUtil
{
    class LightSource;
}

namespace Terrain
{
    class TerrainDrawable;
}

namespace RtxBridge
{
    /// What one extraction pass did.
    ///
    /// The reused counts are the interesting half: a mirror that adds nothing on a second pass over
    /// an unchanged graph is the property the whole incremental design rests on, and it is only
    /// visible as a number.
    struct ExtractionStats
    {
        /// Distinct geometry met for the first time, so one new entry in the scene each.
        std::uint32_t mMeshesAdded = 0;
        std::uint32_t mMaterialsAdded = 0;

        /// Drawables that resolved to something already known. A count of lookups, not of meshes:
        /// a hundred crates sharing one model contribute a hundred here and one above.
        std::uint32_t mMeshesReused = 0;
        std::uint32_t mMaterialsReused = 0;
        std::uint32_t mInstances = 0;

        /// Drawables that carry no `osg::Geometry` this can read: skinned and morphed geometry,
        /// whose vertices are produced during cull. M12's problem, counted so it is not a surprise.
        std::uint32_t mSkippedDeformed = 0;

        /// What the textures a scene reached for turned out to be.
        ///
        /// Kept because the answer decides how they are uploaded, and guessing it from what the
        /// content files ought to contain is how a renderer ends up with a path nothing takes.
        std::map<std::string, std::uint32_t> mTextureFormats;

        /// Geometry with no vertices or no triangles. Morrowind ships some.
        std::uint32_t mSkippedEmpty = 0;

        /// Lamps taken off the graph. Zero in the harness, which has no `LightManager` and reads
        /// the cell's `LIGH` records instead.
        std::uint32_t mLights = 0;

        ExtractionStats& operator+=(const ExtractionStats& other);
    };

    /// Mirrors an OpenSceneGraph subtree into a `Rtx::SceneDesc`.
    ///
    /// The identity maps live across calls, so the same geometry met again — in another cell, under
    /// another reference, in a later frame — resolves to the mesh already uploaded rather than to a
    /// copy of it. That is what makes an incremental mirror possible instead of a rebuild per frame,
    /// and it is why this is an object rather than a function.
    class SceneExtractor
    {
    public:
        explicit SceneExtractor(Rtx::SceneDesc& scene)
            : mScene(scene)
        {
        }

        SceneExtractor(const SceneExtractor&) = delete;
        SceneExtractor& operator=(const SceneExtractor&) = delete;

        /// Walks `node` and appends what it finds, placing it by `transform`.
        ///
        /// Takes a const reference because nothing here writes to the graph; OSG's visitor API is
        /// non-const throughout regardless, so the cast happens once, here.
        /// @param frame which of a `SceneUtil::LightSource`'s two buffers to read. The game passes
        ///        the viewer's frame number, which is the one update has just finished writing;
        ///        anything with no `LightManager` in its graph can leave it.
        ExtractionStats extract(const osg::Node& node, const osg::Matrixf& transform, std::size_t frame = 0);

        /// Ends a frame: what was placed becomes what was placed before.
        ///
        /// **Called once per frame by whoever is mirroring a live graph, and never by anything that
        /// walks a world once.** Until it is called, every instance's previous transform is its
        /// current one, which is exactly right for a scene that does not move — and a harness that
        /// loads a region and flies a camera round it wants that answer, not a stale one.
        void advance();

        /// Places one light. **The graph and not the content files**, because that is where a light
        /// that moves with the thing carrying it exists: a torch in an NPC's hand is no cell
        /// record, and neither is a lamp something picked up and put down.
        void addLight(const SceneUtil::LightSource& source, const osg::NodePath& path, const osg::Matrixf& root,
            std::size_t frame, ExtractionStats& stats);

        /// Resolves one drawable and places it. The visitor's whole contract with this class.
        ///
        /// `path` is the node path down to the drawable, which is both how the world transform is
        /// computed and where the state that shades it comes from.
        void addDrawable(
            const osg::Geometry& geometry, const osg::NodePath& path, const osg::Matrixf& root, ExtractionStats& stats);

    private:
        /// What identifies one placement from one frame to the next.
        ///
        /// **The node path, hashed.** A drawable alone is not an instance — a hundred crates share
        /// one geometry — and the path down to it is what tells them apart. Hashed rather than kept,
        /// because a path is a vector of pointers per instance and the map is walked every frame; a
        /// collision costs one object one frame of wrong motion, and at sixty-four bits over tens of
        /// thousands of instances it is not a thing that happens.
        static std::size_t identify(const osg::NodePath& path);

        Rtx::Index resolveMesh(const osg::Geometry& geometry, ExtractionStats& stats);
        Rtx::Index resolveMaterial(const osg::NodePath& path, ExtractionStats& stats);

        /// The layered material of a terrain chunk, whose shading is not on the graph at all.
        ///
        /// `Terrain::TerrainDrawable` carries one pass per ground texture, each alpha-blended over
        /// the last across the same triangles, because that is how a rasterizer draws a blend it
        /// cannot sample in one go. A ray tracer hits the ground once and shades it once, so the
        /// passes are read back into layers and summed there instead.
        Rtx::Index resolveTerrainMaterial(const Terrain::TerrainDrawable& terrain, ExtractionStats& stats);

        Rtx::SceneDesc& mScene;

        /// Where each placement stood, this frame and last. Swapped by `advance`.
        std::unordered_map<std::size_t, osg::Matrixf> mStanding;
        std::unordered_map<std::size_t, osg::Matrixf> mStood;

        // Keyed on pointer identity, which OpenMW's resource cache and its optimizer's
        // SHARE_DUPLICATE_STATE pass together make meaningful: the same model loaded twice is the
        // same object, and equivalent state sets are collapsed into one.
        std::unordered_map<const osg::Geometry*, Rtx::Index> mMeshes;
        std::unordered_map<const osg::StateSet*, Rtx::Index> mMaterials;

        // Refilled per drawable rather than reallocated, because a cell is tens of thousands of them.
        std::vector<std::uint32_t> mIndexScratch;
        std::vector<osg::Vec3f> mNormalScratch;
        std::vector<osg::Vec2f> mTexCoordScratch;
        std::vector<float> mMaskScratch;
    };
}
