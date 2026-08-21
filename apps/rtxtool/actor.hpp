#pragma once

#include <cstddef>
#include <memory>
#include <string>

// Complete rather than forward declared, because `getRoot` hands out a `Group` and every caller
// then wants it as the `osg::Node&` an extractor takes.
#include <osg/Group>
#include <osg/Matrixf>

#include <components/vfs/pathutil.hpp>

namespace osgUtil
{
    class UpdateVisitor;
}

namespace RtxTool
{
    class World;

    /// What an actor is made of, before anything has posed it.
    ///
    /// **A separate step because the two kinds of actor are assembled differently and posed
    /// identically.** A creature is one skinned file; a person is a shared skeleton with a limb hung
    /// on each bone. Once either exists it is the same problem — bones the keyframes move and skin
    /// that follows them — which is what `Actor` is.
    struct ActorModel
    {
        /// The graph, with a `SceneUtil::Skeleton` at its root.
        osg::ref_ptr<osg::Group> mRoot;

        /// What was actually loaded, which is never quite what was asked for: the `x`-prefixed
        /// skeleton beside a creature's model, or the base animation a person's parts hang on. The
        /// keyframes are the same path with a different extension.
        VFS::Path::Normalized mSkeleton;
    };

    /// A creature: one skinned file, with its animation beside it under the same name.
    ///
    /// @param model the reference's model path — `meshes/r/cliffracer.nif`, not the `x`-prefixed
    ///        skinned one. Morrowind keeps an actor's skeleton in a second file beside the static
    ///        model and names it by convention, so the correction happens here and a caller passes
    ///        what a `CREA` record holds.
    ActorModel loadCreature(World& world, VFS::Path::NormalizedView model);

    /// One animated actor, loaded and posed with no game running and no window open.
    ///
    /// **A pose and not an animation system.** `MWRender::Animation` is two thousand lines about
    /// which animation plays, when it stops, what blends into it and how far it moved the thing
    /// playing it — every one of those a question about the simulation. A mirror cannot tell one
    /// pose from another, so this asks the only question it can answer: put the bones where the
    /// keyframes say they are at this time, and skin to them.
    ///
    /// It exists because the harness could not show an actor at all, and a skinned body is the one
    /// thing in the game whose vertices change every frame — so the only way to see whether the RT
    /// path handled one was to open the game and look at it.
    class Actor
    {
    public:
        /// @param model what to pose, from `loadCreature` or `buildNpc`.
        /// @param transform where in the world it stands.
        Actor(World& world, ActorModel model, const osg::Matrixf& transform);
        ~Actor();

        Actor(const Actor&) = delete;
        Actor& operator=(const Actor&) = delete;

        /// Puts the bones where the keyframes have them at `seconds`, and the skin on the bones.
        ///
        /// Wrapped to the track's own length, so every time is a valid time and a caller stepping a
        /// clock never has to know how long the animation is.
        void pose(float seconds);

        /// How long the whole keyframe track runs, in seconds. Zero where there was none to load.
        float getDuration() const { return mDuration; }

        /// How many of the skeleton's bones the keyframes reached.
        ///
        /// **Zero is the failure that looks like success**: a keyframe file whose bone names miss
        /// the skeleton's loads without complaint, attaches nothing, and poses an actor that stands
        /// perfectly still — which is also what a correct actor holding a still frame looks like.
        std::size_t getPosedBones() const { return mPosedBones; }

        /// What was loaded, which is not what was asked for: the `x`-prefixed skeleton beside it.
        const VFS::Path::Normalized& getSkeleton() const { return mModel.mSkeleton; }

        osg::Group& getRoot() const { return *mModel.mRoot; }
        const osg::Matrixf& getTransform() const { return mTransform; }

    private:
        class Clock;
        class PoseCull;

        /// Held rather than made per pose, because the controllers hold a pointer to it.
        std::shared_ptr<Clock> mClock;

        std::unique_ptr<PoseCull> mCull;
        std::unique_ptr<osgUtil::UpdateVisitor> mUpdate;

        ActorModel mModel;
        osg::Matrixf mTransform;
        float mDuration = 0.0f;
        std::size_t mPosedBones = 0;

        /// Both traversals are keyed on this and both refuse to run twice on the same number, which
        /// is how OpenMW keeps a frame from skinning an actor once per camera. A pose is a frame as
        /// far as they are concerned, so it counts.
        unsigned int mTraversal = 0;
    };

    /// Where the `index`th of `count` actors stands in front of a camera, facing it.
    ///
    /// **Aimed rather than given.** An actor is put into a shot to be looked at, and the one place
    /// it is certain to be looked at is where the camera already points — so it goes at the target,
    /// and several of them go in a row across the view. Standing them upright whatever the camera's
    /// pitch, because a creature leaning back at the angle you happened to look down from is a
    /// picture of the harness rather than of the creature.
    osg::Matrixf placeActor(const osg::Vec3f& origin, const osg::Vec3f& target, std::size_t index, std::size_t count);
}
