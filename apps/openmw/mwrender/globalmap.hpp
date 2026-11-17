#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <osg/ref_ptr>

namespace osg
{
    class Texture2D;
    class Image;
}

namespace MyGUI
{
    class ITexture;
}

namespace ESM
{
    struct GlobalMap;
}

namespace SceneUtil
{
    class WorkQueue;
}

namespace MWRender
{

    class CreateMapWorkItem;
    class LocalMap;
    class MapOverlay;
    class Renderer;

    class GlobalMap
    {
    public:
        /// @param renderer which makes the overlay the explored cells are painted into
        GlobalMap(Renderer& renderer, const std::shared_ptr<SceneUtil::WorkQueue>& workQueue);
        ~GlobalMap();

        void render();

        std::int32_t getWidth() const { return mWidth; }
        std::int32_t getHeight() const { return mHeight; }

        void worldPosToImageSpace(float x, float z, float& imageX, float& imageY);

        /// Paints the local map's picture of a cell into the overlay, where the local map has one:
        /// a cell it has not been asked for paints nothing.
        void exploreCell(std::int32_t cellX, std::int32_t cellY, LocalMap& localMap);

        /// Clears the overlay
        void clear();

        void write(ESM::GlobalMap& map);
        void read(ESM::GlobalMap& map);

        /// The base map, for a widget to show.
        std::unique_ptr<MyGUI::ITexture> makeBaseGuiTexture();

        /// The overlay's picture, which the overlay owns: whichever renderer paints it hands it out.
        MyGUI::ITexture& getOverlayTexture();

        void ensureLoaded();

        void asyncWritePng();

    private:
        struct WritePng;

        Renderer& mRenderer;

        osg::ref_ptr<osg::Texture2D> mBaseTexture;

        // The explored cells over the base, as whichever renderer draws paints them
        std::unique_ptr<MapOverlay> mOverlay;

        std::shared_ptr<SceneUtil::WorkQueue> mWorkQueue;
        std::shared_ptr<CreateMapWorkItem> mWorkItem;
        std::shared_ptr<WritePng> mWritePng;

        std::int32_t mWidth;
        std::int32_t mHeight;

        std::int32_t mMinX, mMaxX, mMinY, mMaxY;
    };

}
