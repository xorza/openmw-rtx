#pragma once

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
    /// Kept among the node's user objects and not in its user data slot, which `StableIdentity`
    /// holds on the game's roots and the editor's tags hold on every node it hands `addLight`.
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

        /// Marks `group` as the model of `light`, in place of the marker of a light hung there before.
        static void mark(osg::Node& group, LightSource& light)
        {
            osg::UserDataContainer& held = *group.getOrCreateUserDataContainer();
            const unsigned int at = indexIn(held);
            if (at < held.getNumUserObjects())
                held.setUserObject(at, new LampBody(light));
            else
                held.addUserObject(new LampBody(light));
        }

        /// The marker `node` carries, or null where it carries none.
        static const LampBody* find(const osg::Node& node)
        {
            const osg::UserDataContainer* held = node.getUserDataContainer();
            if (held == nullptr)
                return nullptr;

            const unsigned int at = indexIn(*held);
            if (at == held->getNumUserObjects())
                return nullptr;
            return static_cast<const LampBody*>(held->getUserObject(at));
        }

        /// The light this group is the model of, or null where it has gone: a light detached
        /// before its model leaves a marker naming nothing.
        const LightSource* getLight() const { return mLight.get(); }

    private:
        /// Where among `held`'s user objects the marker is, or their count where it is none of them.
        /// An exact type test, as `StableIdentity::find` makes for the same reason.
        static unsigned int indexIn(const osg::UserDataContainer& held)
        {
            const unsigned int count = held.getNumUserObjects();
            for (unsigned int at = 0; at < count; ++at)
            {
                const osg::Object* object = held.getUserObject(at);
                if (object != nullptr && typeid(*object) == typeid(LampBody))
                    return at;
            }
            return count;
        }

        osg::observer_ptr<LightSource> mLight;
    };
}
