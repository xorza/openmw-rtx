#pragma once

#include <vector>

#include <osg/Matrixf>
#include <osg/NodeVisitor>

#include <components/rtx/frame/debuglines.hpp>

namespace osg
{
    class Drawable;
    class Node;
    class Transform;
}

namespace MWRender
{
    /// Reads what the game's debug modes drew this frame off the world root — every
    /// `osg::Geometry` under a node of `Mask_Debug` — into the flat lists the ray tracer's line pass
    /// draws: the navmesh, the pathgrid, the actors' paths, the recast mesh, the collision shapes,
    /// the cell borders.
    ///
    /// **Culled by the view's own mask, as the rasterizer culls them**, so debug geometry hung under
    /// a class the view hides goes with it: the cell borders stand under a `Mask_Terrain` group, and
    /// `tws` takes them with the ground. Less `Mask_Scene`, the root of the world itself, which holds
    /// no debug node and which a walk each frame would otherwise enter whole. The mirror's walk never
    /// enters `Mask_Debug`: a line is nothing for a ray to meet. Every primitive set is taken apart
    /// into lines and triangles — a strip, a fan and a quad included — by the world transform in
    /// force at the drawable, with the colour the drawer painted at each vertex or over the whole.
    /// Points are not drawn: nothing here has a size for one.
    ///
    /// **The polygon mode and the blend mode along the path are kept**, as the rasterizer's state
    /// stack keeps them: under `PolygonMode::LINE`, which the collision drawer and the cell borders
    /// set, a triangle or a quad is its edges and not a face; and where nothing turns `GL_BLEND`
    /// on, a colour's alpha is not read — the cell borders paint their yellow at an alpha of
    /// nought, and only the navmesh blends.
    ///
    /// The lists live across frames and are refilled: a walk allocates nothing after the busiest
    /// frame so far.
    class DebugWalk : public osg::NodeVisitor
    {
    public:
        DebugWalk();

        /// Walks `root` under the view mask `view` and answers what stands under its debug nodes.
        /// Empty on every frame no mode is on, at the cost of visiting the root's own children and
        /// entering few.
        Rtx::DebugLines walk(osg::Node& root, unsigned int view);

        void apply(osg::Node& node) override;
        void apply(osg::Transform& transform) override;
        void apply(osg::Drawable& drawable) override;

        /// What the state sets along the path leave in force where the walk stands.
        struct Drawn
        {
            /// Whether a polygon is drawn as its edges.
            bool mEdges = false;

            /// Whether a colour's alpha blends it over the picture.
            bool mBlends = false;
        };

    private:
        /// What `stateSet` states of the two, over what is above where it leaves either alone.
        void take(const osg::StateSet* stateSet);

        /// Takes what `node` states into what is in force: its state set, and its mask.
        void enter(const osg::Node& node);

        Drawn mDrawn;

        /// Whether the walk stands under a node of `Mask_Debug` alone, where what is drawn is a
        /// debug mode's.
        bool mUnderDebug = false;

        /// The transform in force at the node being applied, world from local.
        osg::Matrixf mHere;

        std::vector<Rtx::DebugVertex> mLines;
        std::vector<Rtx::DebugVertex> mTriangles;
    };
}
