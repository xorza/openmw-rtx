#pragma once

namespace osg
{
    class Node;
}

namespace MWRender
{
    /// The shapes of a groundcover model, with the transforms inside the model baked into them.
    ///
    /// **Upstream drew a plant in its shapes' own space.** `groundcover.vert` turns, scales and moves
    /// a vertex by the plant's own placement and only then by `gl_ModelViewMatrix`, which carries the
    /// transforms inside the model: `Nodes × Instance × vertex`, where every other reference is
    /// `Instance × Nodes × vertex`. A model whose shapes stand under a transform other than the
    /// identity drew its plants moved, turned and scaled by it, where the ray tracer, which stands a
    /// plant as it stands a static, drew them where the game places them.
    class GroundcoverShapes
    {
    public:
        /// Bakes every transform between `model` and each of its geometries into that geometry's
        /// vertices and normals, and sets those transforms to the identity, so the vertex shader's
        /// placement comes after them. The model is the chunk's own deep copy, whose nodes and
        /// arrays nothing else holds.
        ///
        /// **Nothing where a transform is one a bake cannot stand in for**: one with a callback, which
        /// a controller or a billboard writes its matrix through every frame, or one that is no plain
        /// matrix. Such a model keeps upstream's order, and the return value says so.
        static bool bakeTransforms(osg::Node& model);
    };
}
