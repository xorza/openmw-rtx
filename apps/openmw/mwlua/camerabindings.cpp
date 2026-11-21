#include "camerabindings.hpp"

#include <cstdint>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwphysics/raycasting.hpp>
#include <apps/openmw/mwrender/camera.hpp>
#include <apps/openmw/mwrender/renderer.hpp>
#include <apps/openmw/mwrender/renderingmanager.hpp>
#include <components/lua/luastate.hpp>
#include <components/lua/utilpackage.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/finitevalues.hpp>
#include <components/settings/values.hpp>

namespace MWLua
{
    using CameraMode = MWRender::Camera::Mode;

    sol::table initCameraPackage(sol::state_view lua)
    {
        using Misc::FiniteFloat;

        MWRender::Camera* camera = MWBase::Environment::get().getWorld()->getCamera();
        MWRender::RenderingManager* renderingManager = MWBase::Environment::get().getWorld()->getRenderingManager();

        sol::table api(lua, sol::create);
        api["MODE"] = LuaUtil::makeStrictReadOnly(
            lua.create_table_with("Static", CameraMode::Static, "FirstPerson", CameraMode::FirstPerson, "ThirdPerson",
                CameraMode::ThirdPerson, "Vanity", CameraMode::Vanity, "Preview", CameraMode::Preview));

        api["getMode"] = [camera]() -> std::int32_t { return static_cast<std::int32_t>(camera->getMode()); };
        api["getQueuedMode"] = [camera]() -> sol::optional<std::int32_t> {
            std::optional<CameraMode> mode = camera->getQueuedMode();
            if (mode)
                return static_cast<std::int32_t>(*mode);
            else
                return sol::nullopt;
        };
        api["setMode"] = [camera](std::int32_t mode, sol::optional<bool> force) {
            camera->setMode(static_cast<CameraMode>(mode), force ? *force : false);
        };

        api["allowCharacterDeferredRotation"] = [camera](bool v) { camera->allowCharacterDeferredRotation(v); };
        api["showCrosshair"] = [camera](bool v) { camera->showCrosshair(v); };

        api["getTrackedPosition"]
            = [camera]() -> glm::vec3 { return Misc::Convert::toGlm(camera->getTrackedPosition()); };
        api["getPosition"] = [camera]() -> glm::vec3 { return Misc::Convert::toGlm(camera->getPosition()); };

        // All angles are negated in order to make camera rotation consistent with objects rotation.
        // TODO: Fix the inconsistency of rotation direction in camera.cpp.
        api["getPitch"] = [camera]() { return -camera->getPitch(); };
        api["getYaw"] = [camera]() { return -camera->getYaw(); };
        api["getRoll"] = [camera]() { return -camera->getRoll(); };

        api["setStaticPosition"]
            = [camera](const glm::vec3& pos) { camera->setStaticPosition(Misc::Convert::toOsg(pos)); };
        api["setPitch"] = [camera](const FiniteFloat v) {
            camera->setPitch(-v, true);
            if (camera->getMode() == CameraMode::ThirdPerson)
                camera->calculateDeferredRotation();
        };
        api["setYaw"] = [camera](const FiniteFloat v) {
            camera->setYaw(-v, true);
            if (camera->getMode() == CameraMode::ThirdPerson)
                camera->calculateDeferredRotation();
        };
        api["setRoll"] = [camera](const FiniteFloat v) { camera->setRoll(-v); };
        api["setExtraPitch"] = [camera](const FiniteFloat v) { camera->setExtraPitch(-v); };
        api["setExtraYaw"] = [camera](const FiniteFloat v) { camera->setExtraYaw(-v); };
        api["setExtraRoll"] = [camera](const FiniteFloat v) { camera->setExtraRoll(-v); };
        api["setProjectionOffset"] = [renderingManager](const glm::vec2& v) {
            renderingManager->setProjectionOffset(Misc::Convert::toOsg(v));
        };
        api["getExtraPitch"] = [camera]() { return -camera->getExtraPitch(); };
        api["getExtraYaw"] = [camera]() { return -camera->getExtraYaw(); };
        api["getExtraRoll"] = [camera]() { return -camera->getExtraRoll(); };
        api["getProjectionOffset"] = [renderingManager]() -> glm::vec2 {
            return Misc::Convert::toGlm(renderingManager->getProjectionOffset());
        };

        api["getThirdPersonDistance"] = [camera]() { return camera->getCameraDistance(); };
        api["setPreferredThirdPersonDistance"]
            = [camera](const FiniteFloat v) { camera->setPreferredCameraDistance(v); };

        api["getFirstPersonOffset"]
            = [camera]() -> glm::vec3 { return Misc::Convert::toGlm(camera->getFirstPersonOffset()); };
        api["setFirstPersonOffset"]
            = [camera](const glm::vec3& v) { camera->setFirstPersonOffset(Misc::Convert::toOsg(v)); };

        api["getFocalPreferredOffset"]
            = [camera]() -> glm::vec2 { return Misc::Convert::toGlm(camera->getFocalPointTargetOffset()); };
        api["setFocalPreferredOffset"]
            = [camera](const glm::vec2& v) { camera->setFocalPointTargetOffset(Misc::Convert::toOsg(v)); };
        api["getFocalTransitionSpeed"] = [camera]() { return camera->getFocalPointTransitionSpeed(); };
        api["setFocalTransitionSpeed"] = [camera](const FiniteFloat v) { camera->setFocalPointTransitionSpeed(v); };
        api["instantTransition"] = [camera]() { camera->instantTransition(); };

        api["getCollisionType"] = [camera]() { return camera->getCollisionType(); };
        api["setCollisionType"] = [camera](std::int32_t collisionType) { camera->setCollisionType(collisionType); };

        api["getBaseFieldOfView"] = [] { return osg::DegreesToRadians(Settings::camera().mFieldOfView); };
        api["getFieldOfView"]
            = [renderingManager]() { return osg::DegreesToRadians(renderingManager->getFieldOfView()); };
        api["setFieldOfView"]
            = [renderingManager](const FiniteFloat v) { renderingManager->setFieldOfView(osg::RadiansToDegrees(v)); };

        api["getBaseViewDistance"] = [] { return Settings::camera().mViewingDistance.get(); };
        api["getViewDistance"] = [renderingManager]() { return renderingManager->getViewDistance(); };
        api["setViewDistance"]
            = [renderingManager](const FiniteFloat d) { renderingManager->setViewDistance(d, true); };

        api["getViewTransform"]
            = [camera]() { return LuaUtil::TransformM{ Misc::Convert::toGlm(camera->getViewMatrix()) }; };

        // Both viewport<->world helpers keep their math in osg (row-vector) and only convert
        // at the sol boundary; less risk of botching the projection algebra during the flip.
        api["viewportToWorldVector"] = [camera, renderingManager](glm::vec2 pos) -> glm::vec3 {
            const osg::Vec2i frame = renderingManager->getRenderer().getPresentation().mFrame;
            const double aspect = frame.y() == 0 ? 1.0 : static_cast<double>(frame.x()) / frame.y();
            double fovTan = std::tan(osg::DegreesToRadians(renderingManager->getFieldOfView()) / 2);
            osg::Matrixf invertedViewMatrix;
            invertedViewMatrix.invert(camera->getViewMatrix());
            auto x = static_cast<float>(static_cast<double>(pos.x * 2 - 1) * aspect * fovTan);
            auto y = static_cast<float>(static_cast<double>(1 - pos.y * 2) * fovTan);
            return Misc::Convert::toGlm(invertedViewMatrix.preMult(osg::Vec3f(x, y, -1)) - camera->getPosition());
        };

        api["worldToViewportVector"] = [camera, renderingManager](glm::vec3 posIn) {
            const osg::Vec2i frame = renderingManager->getRenderer().getPresentation().mFrame;
            const std::int32_t width = frame.x();
            const std::int32_t height = frame.y();

            const osg::Vec3f pos = Misc::Convert::toOsg(posIn);
            osg::Matrix windowMatrix
                = osg::Matrix::translate(1.0, 1.0, 1.0) * osg::Matrix::scale(0.5 * width, 0.5 * height, 0.5);
            osg::Vec3f vpCoords = pos * (camera->getViewMatrix() * camera->getProjectionMatrix() * windowMatrix);

            // Move 0,0 to top left to match viewportToWorldVector
            vpCoords.y() = static_cast<float>(height) - vpCoords.y();

            // Set the z component to be distance from camera, in world space units
            vpCoords.z() = (pos - camera->getPosition()).length();

            return Misc::Convert::toGlm(vpCoords);
        };

        api["getFocusRay"] = []() { return MWBase::Environment::get().getWorld()->getFocusRay(); };

        return LuaUtil::makeReadOnly(api);
    }

}
