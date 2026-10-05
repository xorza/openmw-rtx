#pragma once

#include <cassert>
#include <typeinfo>

#include <osg/CopyOp>
#include <osg/Node>
#include <osg/Object>
#include <osg/UserDataContainer>
#include <osg/observer_ptr>

#include <components/sceneutil/lightmanager.hpp>

namespace SceneUtil
{
    /// What `addLight` leaves on the group it attaches a light inside: that every drawable under the
    /// group is the model of that light — a lantern's paper, a candle's flame mesh.
    ///
    /// **Why a renderer that bounces light needs to know.** The light already delivers what the
    /// model glows with, so a renderer that also gathered the model's glow by a bounce would light
    /// the room twice. The rasterizer bounces nothing and never asks.
    ///
    /// Kept in the node's user data slot, as `StableIdentity` is and for its reason: a reader finds
    /// it in one load. The slot is free on every node `addLight` is given — an object root or a
    /// shield part — or holds the marker of the light hung there before, and is the instance's own,
    /// since a model is cloned with its user data.
    class LampBody final : public osg::Object
    {
    public:
        LampBody() = default;

        explicit LampBody(LightSource& light)
            : mLight(&light)
        {
        }

        LampBody(const LampBody& copy, const osg::CopyOp& copyop = osg::CopyOp::SHALLOW_COPY)
            : osg::Object(copy, copyop)
            , mLight(copy.mLight)
        {
        }

        META_Object(SceneUtil, LampBody)

        /// Marks `group` as the model of `light`.
        static void mark(osg::Node& group, LightSource& light)
        {
            assert((group.getUserData() == nullptr || find(group) != nullptr)
                && "a lamp body's marker over another user data: the slot holds one");
            group.setUserData(new LampBody(light));
        }

        /// The marker `node` carries, or null where it carries none.
        static const LampBody* find(const osg::Node& node)
        {
            const osg::UserDataContainer* held = node.getUserDataContainer();
            if (held == nullptr)
                return nullptr;

            // An exact type test, as `StableIdentity::find` makes for the same reason.
            const osg::Referenced* data = held->getUserData();
            if (data == nullptr || typeid(*data) != typeid(LampBody))
                return nullptr;

            return static_cast<const LampBody*>(data);
        }

        /// The light this group is the model of, or null where it has gone: a light detached
        /// before its model leaves a marker naming nothing.
        const LightSource* getLight() const { return mLight.get(); }

    private:
        osg::observer_ptr<LightSource> mLight;
    };
}
