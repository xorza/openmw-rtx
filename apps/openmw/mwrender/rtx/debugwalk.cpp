#include "debugwalk.hpp"

#include <cstddef>

#include <osg/Array>
#include <osg/Drawable>
#include <osg/GL>
#include <osg/Geometry>
#include <osg/Matrix>
#include <osg/Matrixf>
#include <osg/Node>
#include <osg/PolygonMode>
#include <osg/PrimitiveSet>
#include <osg/StateSet>
#include <osg/TemplatePrimitiveIndexFunctor>
#include <osg/Transform>
#include <osg/Vec4f>
#include <osg/ref_ptr>

#include "../vismask.hpp"

namespace MWRender
{
    namespace
    {
        /// The colour a vertex was painted, off whichever array and binding the drawer used.
        class Painted
        {
        public:
            Painted(const osg::Geometry& geometry)
                : mFour(dynamic_cast<const osg::Vec4Array*>(geometry.getColorArray()))
                , mThree(dynamic_cast<const osg::Vec3Array*>(geometry.getColorArray()))
                , mPerVertex(geometry.getColorArray() != nullptr
                      && geometry.getColorArray()->getBinding() == osg::Array::BIND_PER_VERTEX)
            {
            }

            osg::Vec4f at(const std::size_t vertex) const
            {
                const std::size_t index = mPerVertex ? vertex : 0;
                if (mFour != nullptr && index < mFour->size())
                    return (*mFour)[index];
                if (mThree != nullptr && index < mThree->size())
                    return osg::Vec4f((*mThree)[index], 1.0f);

                return osg::Vec4f(1.0f, 1.0f, 1.0f, 1.0f);
            }

        private:
            const osg::Vec4Array* mFour;
            const osg::Vec3Array* mThree;
            bool mPerVertex;
        };

        /// What `osg::TemplatePrimitiveIndexFunctor` hands each primitive to: a line's two corners
        /// or a triangle's three, as indices into the drawable's arrays. A quad is two triangles,
        /// and a point is answered with nothing. Pointers and not references, because the functor
        /// builds its base by default and is told what to read afterwards.
        struct Taker
        {
            const osg::Vec3Array* mPositions = nullptr;
            const Painted* mPainted = nullptr;
            const osg::Matrixf* mHere = nullptr;
            std::vector<Rtx::DebugVertex>* mLines = nullptr;
            std::vector<Rtx::DebugVertex>* mTriangles = nullptr;

            DebugWalk::Drawn mDrawn;

            Rtx::DebugVertex vertexAt(const unsigned int index) const
            {
                osg::Vec4f colour = mPainted->at(index);
                if (!mDrawn.mBlends)
                    colour.a() = 1.0f;

                return Rtx::DebugVertex{
                    .mPosition = (*mPositions)[index] * *mHere,
                    .mColour = colour,
                };
            }

            void operator()(unsigned int) const {}

            void operator()(const unsigned int a, const unsigned int b) const
            {
                mLines->push_back(vertexAt(a));
                mLines->push_back(vertexAt(b));
            }

            void operator()(const unsigned int a, const unsigned int b, const unsigned int c) const
            {
                if (mDrawn.mEdges)
                {
                    (*this)(a, b);
                    (*this)(b, c);
                    (*this)(c, a);
                    return;
                }

                mTriangles->push_back(vertexAt(a));
                mTriangles->push_back(vertexAt(b));
                mTriangles->push_back(vertexAt(c));
            }

            /// A quad as its outline under `LINE`, as OpenGL draws one, and no diagonal.
            void operator()(
                const unsigned int a, const unsigned int b, const unsigned int c, const unsigned int d) const
            {
                if (mDrawn.mEdges)
                {
                    (*this)(a, b);
                    (*this)(b, c);
                    (*this)(c, d);
                    (*this)(d, a);
                    return;
                }

                (*this)(a, b, c);
                (*this)(a, c, d);
            }
        };
    }

    DebugWalk::DebugWalk()
        : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
    {
    }

    Rtx::DebugLines DebugWalk::walk(osg::Node& root, const unsigned int view)
    {
        setTraversalMask(view & ~Mask_Scene);
        mLines.clear();
        mTriangles.clear();
        mHere.makeIdentity();
        mDrawn = {};
        mUnderDebug = false;

        root.accept(*this);

        return Rtx::DebugLines{ .mLines = mLines, .mTriangles = mTriangles };
    }

    void DebugWalk::take(const osg::StateSet* stateSet)
    {
        if (stateSet == nullptr)
            return;

        const auto* mode
            = dynamic_cast<const osg::PolygonMode*>(stateSet->getAttribute(osg::StateAttribute::POLYGONMODE));
        if (mode != nullptr)
            mDrawn.mEdges = mode->getMode(osg::PolygonMode::FRONT) == osg::PolygonMode::LINE;

        const osg::StateAttribute::GLModeValue blend = stateSet->getMode(GL_BLEND);
        if (blend != osg::StateAttribute::INHERIT)
            mDrawn.mBlends = (blend & osg::StateAttribute::ON) != 0;
    }

    void DebugWalk::apply(osg::Node& node)
    {
        const Drawn above = mDrawn;
        const bool underAbove = mUnderDebug;
        enter(node);
        traverse(node);
        mDrawn = above;
        mUnderDebug = underAbove;
    }

    void DebugWalk::enter(const osg::Node& node)
    {
        take(node.getStateSet());
        // A node of `Mask_Debug` and no other bit: one whose mask is left at its all-ones default
        // carries the bit too, as every group of the world does.
        mUnderDebug = mUnderDebug || (node.getNodeMask() & ~Mask_Debug) == 0;
    }

    void DebugWalk::apply(osg::Transform& transform)
    {
        const osg::Matrixf above = mHere;
        const Drawn drawnAbove = mDrawn;
        const bool underAbove = mUnderDebug;
        enter(transform);

        // In the graph's own precision, as `computeLocalToWorldMatrix` takes it — `osg::Matrix`
        // is double on this box and single on a distribution's OSG — and back to the single the
        // lines are drawn in.
        osg::Matrix here(mHere);
        transform.computeLocalToWorldMatrix(here, this);
        mHere = osg::Matrixf(here);

        traverse(transform);
        mHere = above;
        mDrawn = drawnAbove;
        mUnderDebug = underAbove;
    }

    void DebugWalk::apply(osg::Drawable& drawable)
    {
        const osg::Geometry* const geometry = drawable.asGeometry();
        if (geometry == nullptr)
            return;

        const auto* const positions = dynamic_cast<const osg::Vec3Array*>(geometry->getVertexArray());
        if (positions == nullptr)
            return;

        const Drawn above = mDrawn;
        const bool underAbove = mUnderDebug;
        enter(drawable);
        if (!mUnderDebug)
        {
            mDrawn = above;
            mUnderDebug = underAbove;
            return;
        }

        const Painted painted(*geometry);
        osg::TemplatePrimitiveIndexFunctor<Taker> taker;
        taker.mDrawn = mDrawn;
        taker.mPositions = positions;
        taker.mPainted = &painted;
        taker.mHere = &mHere;
        taker.mLines = &mLines;
        taker.mTriangles = &mTriangles;

        for (const osg::ref_ptr<osg::PrimitiveSet>& primitives : geometry->getPrimitiveSetList())
            primitives->accept(taker);

        mDrawn = above;
        mUnderDebug = underAbove;
    }
}
