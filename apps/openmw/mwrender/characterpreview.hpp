#pragma once

#include <cstdint>
#include <memory>

#include <osg/PositionAttitudeTransform>
#include <osg/ref_ptr>

#include <apps/openmw/mwworld/ptr.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/resource/resourcesystem.hpp>

#include "offscreenview.hpp"

namespace osg
{
    class Group;
}

namespace MyGUI
{
    class ITexture;
}

namespace MWRender
{

    class NpcAnimation;
    class Renderer;

    class CharacterPreview
    {
    public:
        CharacterPreview(const CharacterPreview&) = delete;
        CharacterPreview& operator=(const CharacterPreview&) = delete;
        CharacterPreview(Renderer& renderer, Resource::ResourceSystem* resourceSystem, const MWWorld::Ptr& character,
            std::int32_t sizeX, std::int32_t sizeY, const osg::Vec3f& position, const osg::Vec3f& lookAt);
        virtual ~CharacterPreview();

        std::int32_t getTextureWidth() const;
        std::int32_t getTextureHeight() const;

        void redraw();

        void rebuild();

        MyGUI::ITexture& getTexture();

        /// For a caller that wants the image rather than the widget: OffscreenView::getCopy carries the row order
        OffscreenView& getView() { return *mView; }

    private:
    protected:
        virtual bool renderHeadOnly() { return false; }
        void setBlendMode();
        virtual void onSetup();

        Resource::ResourceSystem* mResourceSystem;

        // What the view draws. An update callback here runs during the view's own update.
        osg::ref_ptr<osg::Group> mScene;

        std::unique_ptr<SubjectView> mView;

        osg::Vec3f mPosition;
        osg::Vec3f mLookAt;

        MWWorld::Ptr mCharacter;

        std::unique_ptr<MWRender::NpcAnimation> mAnimation;
        osg::ref_ptr<osg::PositionAttitudeTransform> mNode;
        std::string mCurrentAnimGroup;

        std::int32_t mSizeX;
        std::int32_t mSizeY;
    };

    class InventoryPreview : public CharacterPreview
    {
    public:
        InventoryPreview(Renderer& renderer, Resource::ResourceSystem* resourceSystem, const MWWorld::Ptr& character);

        void updatePtr(const MWWorld::Ptr& ptr);

        void update(); // Render preview again, e.g. after changed equipment
        void setViewport(std::int32_t sizeX, std::int32_t sizeY);

        std::int32_t getSlotSelected(std::int32_t posX, std::int32_t posY);

    protected:
        void onSetup() override;

    private:
        // How much of the picture the window shows. Zero until the window has been laid out.
        std::int32_t mExtentX = 0;
        std::int32_t mExtentY = 0;
    };

    class UpdateCameraCallback;

    class RaceSelectionPreview : public CharacterPreview
    {
        ESM::NPC mBase;
        MWWorld::LiveCellRef<ESM::NPC> mRef;

    protected:
        bool renderHeadOnly() override { return true; }
        void onSetup() override;

    public:
        RaceSelectionPreview(Renderer& renderer, Resource::ResourceSystem* resourceSystem);
        ~RaceSelectionPreview() override;

        void setAngle(float angleRadians);

        const ESM::NPC& getPrototype() const { return mBase; }

        void setPrototype(const ESM::NPC& proto);

    private:
        osg::ref_ptr<UpdateCameraCallback> mUpdateCameraCallback;

        float mPitchRadians;
    };

}
