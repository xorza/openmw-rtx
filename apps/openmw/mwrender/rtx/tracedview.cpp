#include "tracedview.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <numbers>

#include <osg/Image>
#include <osgUtil/IntersectionVisitor>
#include <osgUtil/LineSegmentIntersector>

#include <MyGUI_ITexture.h>
#include <MyGUI_RenderManager.h>

#include <components/myguirtx/texture.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/rtx/camera.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/sceneextractor.hpp>
#include <components/rtxbridge/texturebuilder.hpp>

#include "rtxrenderer.hpp"

namespace MWRender::Rtx
{
    namespace
    {
        /// Names have to be unique in MyGUI's own table, and nothing outside ever looks one up.
        unsigned int sNextName = 0;

        /// The rasterizer's diffuse coefficient as an irradiance.
        ///
        /// **Derived rather than picked.** What a light rig in `OffscreenViewSpec` says is what
        /// OpenMW's object shaders meant by it: the fraction of a surface's albedo that comes back
        /// off it when the light is square on. A Lambertian surface returns `albedo / pi * E * cos`,
        /// so the `E` that makes that equal `albedo * diffuse` at `cos = 1` is `diffuse * pi` — and
        /// with it a doll lit by the game's own numbers comes out the brightness those numbers were
        /// chosen for, at the fixed exposure a picture inside the interface is drawn at.
        osg::Vec3f irradianceOf(const osg::Vec4f& colour)
        {
            return osg::Vec3f(colour.r(), colour.g(), colour.b()) * std::numbers::pi_v<float>;
        }

        std::uint8_t channel(float value)
        {
            return static_cast<std::uint8_t>(std::clamp(value, 0.f, 1.f) * 255.f + 0.5f);
        }
    }

    TracedView::TracedView(const OffscreenViewSpec& spec, RtxRenderer& owner, ::Rtx::Renderer& renderer)
        : mOwner(owner)
        , mRenderer(renderer)
        , mSubject(spec.mFromWorld ? nullptr : &spec.mScene)
        , mSubjectMask(spec.mMask)
        , mWidth(spec.mWidth)
        , mHeight(spec.mHeight)
        , mExtentX(spec.mWidth)
        , mExtentY(spec.mHeight)
        , mNear(spec.mNear)
        , mFar(spec.mFar)
        , mSunDirection(-spec.mSunDirection)
        , mSunIrradiance(irradianceOf(spec.mSunDiffuse))
        , mAmbient(irradianceOf(spec.mSunAmbient))
        , mTransparent(spec.mClearColour.a() < 1.f)
        , mFromWorld(spec.mFromWorld)
    {
        if (!mFromWorld)
        {
            mScene = std::make_unique<::Rtx::SceneDesc>();
            mExtractor = std::make_unique<RtxBridge::SceneExtractor>(*mScene);
            mExtractor->setTraversalMask(mSubjectMask);
            mViewScene = renderer.addViewScene();
        }

        if (mSunDirection.length2() > 0.f)
            mSunDirection.normalize();

        if (const auto* perspective = std::get_if<OffscreenViewSpec::Perspective>(&spec.mProjection))
            mFieldOfView = perspective->mFieldOfView;
        else
        {
            const auto& box = std::get<OffscreenViewSpec::Orthographic>(spec.mProjection);
            mPerspective = false;
            mBoxWidth = box.mWidth;
            mBoxHeight = box.mHeight;
        }

        mOptions.mClear
            = { spec.mClearColour.r(), spec.mClearColour.g(), spec.mClearColour.b(), spec.mClearColour.a() };

        mTexture = MyGUI::RenderManager::getInstance().createTexture(std::format("rtx offscreen view {}", sNextName++));
        mTexture->createManual(
            mWidth, mHeight, MyGUI::TextureUsage::Static | MyGUI::TextureUsage::Write, MyGUI::PixelFormat::R8G8B8A8);

        // **The clear colour, before anything has been traced.** A view is shown from the frame it
        // is made on and drawn on some later one — a map tile is asked for as its cell arrives —
        // and the alternative is a widget holding whatever the slot was cleared to.
        const std::uint8_t colour[4] = { channel(spec.mClearColour.r()), channel(spec.mClearColour.g()),
            channel(spec.mClearColour.b()), channel(spec.mClearColour.a()) };

        auto* pixels = static_cast<std::uint8_t*>(mTexture->lock(MyGUI::TextureUsage::Write));
        for (int i = 0; i < mWidth * mHeight; ++i)
            std::memcpy(pixels + i * 4, colour, sizeof(colour));
        mTexture->unlock();

        mSlot = static_cast<MyGUIRtx::Texture*>(mTexture)->getSlot();
    }

    TracedView::~TracedView()
    {
        mOwner.forgetView(*this);

        if (!mFromWorld)
            mRenderer.dropViewScene(mViewScene);

        MyGUI::RenderManager::getInstance().destroyTexture(mTexture);
    }

    void TracedView::setView(const osg::Matrixf& view)
    {
        mView = view;
    }

    void TracedView::setExtent(int width, int height)
    {
        mExtentX = std::clamp(width, 1, mWidth);
        mExtentY = std::clamp(height, 1, mHeight);
    }

    void TracedView::sceneChanged()
    {
        // **Nothing either way, and for two different reasons.** A picture of the world is a picture
        // of the scene the mirror rebuilds every frame regardless; a picture of its own subject
        // walks that subject again on every `redraw`, which is the only time it is looked at.
    }

    ::Rtx::Shaders::VisibilityConstants TracedView::describeCamera() const
    {
        const std::uint32_t width = static_cast<std::uint32_t>(mExtentX);
        const std::uint32_t height = static_cast<std::uint32_t>(mExtentY);

        ::Rtx::Shaders::VisibilityConstants camera = mPerspective
            ? ::Rtx::makeCameraFromView(mView, mFieldOfView, width, height, mNear, mFar)
            : ::Rtx::makeOrthographicCameraFromView(mView, mBoxWidth, mBoxHeight, width, height, mNear, mFar);

        // **Upside down against the frame, and it is the contract.** `OffscreenView::getTexture`
        // hands back a picture with its first row at the *bottom*, because that is what an OpenGL
        // render-to-texture produces and what the widgets showing one already invert V for.
        // Flipping the camera's up vector costs nothing and puts the rows where they are expected.
        camera.mUp = -camera.mUp;

        camera.mSunDirection = mSunDirection;
        camera.mSunIrradiance = mSunIrradiance;
        camera.mAmbient = mAmbient;
        camera.mTransparentBackground = mTransparent ? 1 : 0;

        return camera;
    }

    void TracedView::rebuildSubject()
    {
        Resource::ResourceSystem* resources = mOwner.getResources();
        if (resources == nullptr)
            return;

        // **Posed here, because nothing else will.** The camera callback the game hangs on this
        // subtree is what finds the head to look at, and it runs in an update traversal.
        mPosedFrame = mOwner.updateSubtree(const_cast<osg::Node&>(*mSubject));

        // **From nothing, every time, and that is the point rather than the cost.** A mirror that
        // is kept answers "have I met this drawable before" with an `osg` address, and between one
        // redraw and the next this subject is taken apart: `NpcAnimation::updateParts` frees the
        // body parts that changed and builds their replacements, which the allocator is free to put
        // exactly where the old ones were. A walk that carried its tables across that finds the
        // retired part's entry under the new part's address and mirrors the wrong geometry — the
        // torn figure a change of clothes produced. There is no window here for that: the tables
        // start empty, so nothing can be mistaken for anything.
        //
        // What it costs is re-reading a character's meshes, which is tens of them, on a path that
        // already rebuilds every acceleration structure and the whole texture array below.
        // The extractor first: it holds a reference to the scene it walks into.
        mExtractor.reset();
        mScene = std::make_unique<::Rtx::SceneDesc>();
        mExtractor = std::make_unique<RtxBridge::SceneExtractor>(*mScene);
        mExtractor->setTraversalMask(mSubjectMask);

        // **A clock that reads differently every time, and it outlives the tables above.** The walk
        // poses the subject by running a cull traversal over it, and `SceneUtil::Skeleton` and both
        // deforming geometries — which are the game's and survive every rebuild — refuse to move
        // for a traversal number they have already seen. A walk that said zero every time skinned
        // the doll when the inventory first opened and never again.
        mExtractor->extract(*mSubject, osg::Matrixf::identity(), 0, mRedraws++);

        // Described whole rather than by arrivals: this is a rebuild, and there is nothing to append
        // to. A character is tens of textures, not the hundreds a cell carries.
        const RtxBridge::SceneTextures textures(*mScene, *resources->getImageManager());
        mRenderer.setViewScene(mViewScene, *mScene, textures.getDescriptions());
        mScene->clearArrivals();

        mOptions.mScene = mViewScene;
    }

    void TracedView::redraw()
    {
        // Whatever is in the copy is a picture of the last redraw, and this is a new one.
        mCopyIsCurrent = false;

        if (!mFromWorld)
        {
            rebuildSubject();

            if (mScene->getPlacedCount() == 0)
                return;
        }
        // **Asked for before there is a world, every time a game starts.** A cell asks for its map
        // tile as it loads, which is the frame before the one that first mirrors it; the tile is
        // drawn when there is something to draw it against rather than left blank until the local
        // map happens to ask again.
        else if (!mOwner.hasScene())
        {
            mOwner.deferRedraw(*this);
            return;
        }

        mOptions.mWidth = static_cast<std::uint32_t>(mExtentX);
        mOptions.mHeight = static_cast<std::uint32_t>(mExtentY);

        mRenderer.traceGuiTexture(mSlot, describeCamera(), mOptions);

        if (!mKeepCopy)
            return;

        // **The whole texture and not the extent**, because the copy is what the global map paints
        // a cell from and a cell is the whole tile. The read is the only time a picture inside the
        // interface comes back to main memory, which is why it is asked for rather than always done.
        mRenderer.readGuiTexture(mSlot, mPixels);
        std::memcpy(mCopy->data(), mPixels.data(), std::min<std::size_t>(mPixels.size(), mCopy->getTotalSizeInBytes()));

        mCopyIsCurrent = true;
    }

    void TracedView::keepCopy()
    {
        if (mKeepCopy)
            return;

        mKeepCopy = true;
        mCopy = new osg::Image;
        mCopy->allocateImage(mWidth, mHeight, 1, GL_RGBA, GL_UNSIGNED_BYTE);
        std::memset(mCopy->data(), 0, mCopy->getTotalSizeInBytes());
    }

    const osg::Image* TracedView::getCopy() const
    {
        return mCopyIsCurrent ? mCopy.get() : nullptr;
    }

    bool TracedView::pick(float x, float y, osg::NodePath& hit) const
    {
        // Only the inventory doll asks — what is under the cursor on a map is a question the map
        // answers from where the tile sits, not from what a ray found.
        if (mSubject == nullptr)
            return false;

        // **On the processor and against the graph, not on the device.** What the caller wants back
        // is the node path a click landed on, so it can ask the animation which equipment slot that
        // was; a ray query gives an instance index in a mirror, which is the wrong side of the
        // question. The ray itself is the one the trace would have sent through that point, built
        // from the same basis so what a click finds is what the picture shows.
        const ::Rtx::Shaders::VisibilityConstants camera = describeCamera();
        const osg::Vec3f direction = camera.mForward + camera.mRight * x - camera.mUp * y;

        osg::ref_ptr<osgUtil::LineSegmentIntersector> intersector = new osgUtil::LineSegmentIntersector(
            osgUtil::Intersector::MODEL, camera.mOrigin + direction * mNear, camera.mOrigin + direction * mFar);
        intersector->setIntersectionLimit(osgUtil::LineSegmentIntersector::LIMIT_NEAREST);

        osgUtil::IntersectionVisitor visitor(intersector);
        visitor.setTraversalMode(osg::NodeVisitor::TRAVERSE_ACTIVE_CHILDREN);

        // The frame the pose was written for, so a skinned mesh hands over the buffer the picture
        // was made from rather than the one it will be posed into next.
        visitor.setTraversalNumber(mPosedFrame);

        const_cast<osg::Node&>(*mSubject).accept(visitor);

        if (!intersector->containsIntersections())
            return false;

        hit = intersector->getFirstIntersection().nodePath;
        return true;
    }

}
