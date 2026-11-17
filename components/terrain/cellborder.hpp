#pragma once

#include <cstdint>
#include <map>

#include <osg/Group>

#include <components/esm/refid.hpp>

namespace Resource
{
    class SceneManager;
}

namespace Terrain
{
    class Storage;
    class World;

    /**
     * @Brief Handles the debug cell borders.
     */
    class CellBorder
    {
    public:
        typedef std::map<std::pair<std::int32_t, std::int32_t>, osg::ref_ptr<osg::Node>> CellGrid;

        CellBorder(
            Terrain::World* world, osg::Group* root, std::int32_t borderMask, Resource::SceneManager* sceneManager);

        void createCellBorderGeometry(std::int32_t x, std::int32_t y);
        void destroyCellBorderGeometry(std::int32_t x, std::int32_t y);

        /**
          Destroys the geometry for all borders.
        */
        void destroyCellBorderGeometry();

        static osg::ref_ptr<osg::Group> createBorderGeometry(float x, float y, float size, Storage* terrain,
            Resource::SceneManager* sceneManager, std::int32_t mask, ESM::RefId worldspace, float offset = 10.0,
            osg::Vec4f color = { 1, 1, 0, 0 });

    protected:
        Terrain::World* mWorld;
        Resource::SceneManager* mSceneManager;
        osg::Group* mRoot;

        CellGrid mCellBorderNodes;
        std::int32_t mBorderMask;
    };
}
