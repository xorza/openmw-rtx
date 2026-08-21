#include "actor.hpp"

#include <algorithm>
#include <cmath>

#include <osg/Group>
#include <osg/Viewport>
#include <osgUtil/CullVisitor>
#include <osgUtil/RenderStage>
#include <osgUtil/UpdateVisitor>

#include <components/debug/debuglog.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/resource/keyframemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/keyframe.hpp>
#include <components/sceneutil/skeleton.hpp>
#include <components/sceneutil/visitor.hpp>
#include <components/vfs/manager.hpp>

#include "world.hpp"

namespace RtxTool
{
    /// The one number every keyframe controller on this actor reads.
    class Actor::Clock : public SceneUtil::ControllerSource
    {
    public:
        float mSeconds = 0.0f;

        float getValue(osg::NodeVisitor*) override { return mSeconds; }
    };

    /// A cull traversal that culls nothing and draws nothing.
    ///
    /// **A real `osgUtil::CullVisitor` rather than something claiming to be one.** Skinning happens
    /// in `RigGeometry::cull`, and `RigGeometry::accept` reaches it by casting the visitor to a
    /// `CullVisitor` and pushing the drawable's own state set onto it — which every body part has.
    /// A plain `osg::NodeVisitor` with its type set to `CULL_VISITOR` is that cast being wrong, and
    /// wrong in the way that runs correctly on an untextured test quad.
    ///
    /// What it is not is a *rendering* cull: the drawables it reaches are dropped rather than
    /// binned, and culling is off because posing an actor has no frustum to cull against.
    ///
    /// It is still given a render stage, though nothing will ever be drawn out of it. A state set
    /// that names a render bin sends the visitor to `_currentRenderBin` on the way past, and a good
    /// deal of Morrowind's content names one — the error marker a missing model resolves to among
    /// them, so the first thing this crashed on was a typo in a model path.
    class Actor::PoseCull : public osgUtil::CullVisitor
    {
    public:
        PoseCull()
        {
            setStateGraph(new osgUtil::StateGraph);
            setRenderStage(new osgUtil::RenderStage);
            setCullingMode(osg::CullSettings::NO_CULLING);

            // `CullStack` reads the back of each of these without checking, so they are pushed once
            // and never popped: an empty stack is not a permissive one, it is a crash.
            pushViewport(new osg::Viewport(0, 0, 1, 1));
            pushProjectionMatrix(new osg::RefMatrix);
            pushModelViewMatrix(new osg::RefMatrix, osg::Transform::ABSOLUTE_RF);
        }

        void apply(osg::Drawable&) override {}
    };

    namespace
    {
        /// Where the animation for `model` lives, by Morrowind's own convention.
        ///
        /// `correctActorModelPath` has already turned `cliffracer.nif` into `xcliffracer.nif`; the
        /// keyframes sit beside it under the same name.
        VFS::Path::Normalized keyframesFor(VFS::Path::NormalizedView skeleton)
        {
            VFS::Path::Normalized keyframes(skeleton);
            keyframes.changeExtension(VFS::Path::ExtensionView("kf"));
            return keyframes;
        }
    }

    Actor::Actor(World& world, VFS::Path::NormalizedView model, const osg::Matrixf& transform)
        : mClock(std::make_shared<Clock>())
        , mCull(std::make_unique<PoseCull>())
        , mUpdate(std::make_unique<osgUtil::UpdateVisitor>())
        , mTransform(transform)
    {
        Resource::ResourceSystem& resources = world.getResourceSystem();

        const VFS::Path::Normalized skeleton = Misc::ResourceHelpers::correctActorModelPath(model, resources.getVFS());
        mSkeleton = skeleton.value();

        // An instance and not a template: the controllers below are attached to its nodes, and a
        // template is shared with every other reference to the same model.
        osg::ref_ptr<osg::Node> created = resources.getSceneManager()->getInstance(skeleton);

        // A model with no skinning in it loads as a plain group, and one wrapped in a skeleton it
        // has no bones for is still a thing worth placing — the pose simply does nothing to it.
        mRoot = dynamic_cast<SceneUtil::Skeleton*>(created.get());
        if (mRoot == nullptr)
        {
            osg::ref_ptr<SceneUtil::Skeleton> wrapper = new SceneUtil::Skeleton;
            wrapper->addChild(created);
            mRoot = wrapper;
        }

        const VFS::Path::Normalized keyframes = keyframesFor(skeleton);
        if (!resources.getVFS()->exists(keyframes))
        {
            Log(Debug::Warning) << "No animation beside " << skeleton << ", so " << model << " stands still";
            return;
        }

        osg::ref_ptr<const SceneUtil::KeyframeHolder> track = resources.getKeyframeManager()->get(keyframes);
        if (track == nullptr)
            return;

        SceneUtil::NodeMap bones;
        SceneUtil::NodeMapVisitor collect(bones);
        mRoot->accept(collect);

        for (const auto& [name, controller] : track->mKeyframeControllers)
        {
            const auto bone = bones.find(Misc::StringUtils::lowerCase(name));
            if (bone == bones.end())
                continue;

            // Cloned because the source is what makes a controller answer, and the manager's copy is
            // shared with every other actor of this kind. Shallow, because the keys behind it are
            // the same keys and are read and not written.
            osg::ref_ptr<SceneUtil::KeyframeController> posed = osg::clone(controller.get(), osg::CopyOp::SHALLOW_COPY);
            posed->setSource(mClock);
            bone->second->addUpdateCallback(posed->getAsCallback());

            if (const std::shared_ptr<SceneUtil::ControllerFunction> function = posed->getFunction())
                mDuration = std::max(mDuration, function->getMaximum());

            ++mPosedBones;
        }
    }

    // Out of line because the members it destroys are only forward declared in the header.
    Actor::~Actor() = default;

    osg::Matrixf placeActor(const osg::Vec3f& origin, const osg::Vec3f& target, std::size_t index, std::size_t count)
    {
        /// Wide enough that two of the game's largest creatures stand clear of one another.
        constexpr float sSpacing = 200.0f;

        osg::Vec3f forward = target - origin;
        forward.z() = 0.0f;
        if (forward.normalize() == 0.0f)
            forward = osg::Vec3f(0.0f, 1.0f, 0.0f);

        const osg::Vec3f across(forward.y(), -forward.x(), 0.0f);
        const float offset = (static_cast<float>(index) - static_cast<float>(count - 1) * 0.5f) * sSpacing;

        // Morrowind models its actors facing +Y, so turning +Y round to the way back to the eye is
        // what makes one look at whoever is looking at it.
        osg::Matrixf transform = osg::Matrixf::rotate(osg::Vec3f(0.0f, 1.0f, 0.0f), -forward);
        transform.setTrans(target + across * offset);
        return transform;
    }

    void Actor::pose(float seconds)
    {
        mClock->mSeconds = mDuration > 0.0f ? std::fmod(std::fmod(seconds, mDuration) + mDuration, mDuration) : 0.0f;

        // **Update then cull, and never the same number twice.** The bones move under the update
        // traversal and the skin follows them under the cull; both keep the last number they saw and
        // return early on a repeat, which in the game stops a second camera skinning the same actor
        // again and here would silently stop a pose from happening at all.
        ++mTraversal;

        mUpdate->setTraversalNumber(mTraversal);
        mRoot->accept(*mUpdate);

        mCull->setTraversalNumber(mTraversal);
        mRoot->accept(*mCull);
    }
}
