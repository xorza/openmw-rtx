#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <osg/Node>
#include <osg/Vec3d>
#include <osg/ref_ptr>

#include "object.hpp"

namespace osg
{
    class Camera;
}

namespace SceneUtil
{
    class Material;
}

namespace CSVRender
{
    using NodeMap = std::unordered_map<std::string, osg::ref_ptr<osg::Node>>;
    class WorldspaceWidget;

    class ObjectMarkerTag : public ObjectTag
    {
    public:
        ObjectMarkerTag(Object* object, std::int32_t axis);

        std::int32_t mAxis;
    };

    class ObjectMarker
    {
        friend class WorldspaceWidget;

        WorldspaceWidget* mWorldspaceWidget;
        Resource::ResourceSystem* mResourceSystem;
        NodeMap mMarkerNodes;
        osg::ref_ptr<osg::PositionAttitudeTransform> mBaseNode;
        osg::ref_ptr<osg::PositionAttitudeTransform> mRootNode;
        std::unordered_map<std::string, osg::Vec4f> mOriginalColors;
        std::vector<std::string> mSelectionHistory;
        std::string mLastHitNode;
        std::unordered_map<std::string, SceneUtil::Material*> mLastHighlightedNodes;
        float mMarkerScale;
        std::int32_t mSubMode;

        ObjectMarker(WorldspaceWidget* worldspaceWidget, Resource::ResourceSystem* resourceSystem);

        static std::unique_ptr<ObjectMarker> create(WorldspaceWidget* widget, Resource::ResourceSystem* resourceSystem)
        {
            return std::unique_ptr<ObjectMarker>(new ObjectMarker(widget, resourceSystem));
        }

        bool attachMarker(const std::string& refId);

        void removeFromSelectionHistory(const std::string& refId);

    public:
        ObjectMarker(ObjectMarker&) = delete;
        ObjectMarker(ObjectMarker&&) = delete;
        ObjectMarker& operator=(const ObjectMarker&) = delete;
        ObjectMarker& operator=(ObjectMarker&&) = delete;
        ~ObjectMarker() = default;

        void toggleVisibility();

        bool hitBehindMarker(const osg::Vec3d& hitPos, const osg::ref_ptr<osg::Camera>& camera);

        void detachMarker();

        void addToSelectionHistory(const std::string& refId, bool update = true);

        void updateSelectionMarker(const std::string& refId = std::string());

        void resetMarkerHighlight();

        void updateMarkerHighlight(const std::string_view hitNode, const std::int32_t axis);

        void setSubMode(const std::int32_t subMode);

        void updateScale(const float scale);
    };
}
