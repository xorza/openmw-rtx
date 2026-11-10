#pragma once

namespace Rtx
{
    /// What a shape's triangles turned out to be, once its reversed twins were folded away. Two
    /// facts and neither follows from the other: a shape may be both — two exteriors hold one
    /// each — so `ShapeFold` reports them apart rather than as one kind.
    struct FoldedShape
    {
        /// Every triangle was one of a reversed pair, so the content doubled the whole shape for its
        /// back: a leaf, a fern, a grass card, a tabard. What that is for is `ShapeFold`'s doc, and
        /// a shader reads it as leave to light the mesh through its back.
        bool mSheet = false;

        /// Some triangle was one of a reversed pair, so what is left of this shape stands for both
        /// faces of itself wherever a twin went. `mSheet` is the whole shape and this is any part
        /// of it: an awning with a doubled hem on a single-sided canopy is one mesh, and the hem's
        /// twin goes while the canopy's triangles stand alone.
        ///
        /// **What keeps a ray that draws from culling such a mesh.** The fold left one triangle
        /// where the content drew two, so culling the one by its winding takes the hem out of the
        /// frame from the side whose copy was dropped. The whole mesh is spared, which costs the
        /// canopy its culling and is the conservative half of a question only a per-triangle answer
        /// settles.
        bool mFolded = false;

        /// A wall of a pocket went, so the wall across from it now answers for both faces of the
        /// cloth — what `ShapeFold` says a pocket is. Apart from `mFolded` because it is another
        /// finding and a report counts it apart; a ray that draws spares the mesh for either.
        bool mPocketed = false;
    };
}
