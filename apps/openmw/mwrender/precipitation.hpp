#ifndef GAME_RENDER_PRECIPITATION_H
#define GAME_RENDER_PRECIPITATION_H

#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <components/vfs/pathutil.hpp>

namespace osg
{
    class Camera;
    class Group;
    class Node;
    class PositionAttitudeTransform;
}

namespace osgParticle
{
    class BoxPlacer;
    class ParticleSystem;
}

namespace Resource
{
    class SceneManager;
}

namespace MWRender
{
    class RainCounter;
    class RainShooter;
    struct WeatherResult;

    /// What the weather drops: rain, snow, and the clouds a storm drives past the eye.
    ///
    /// **Renderer-neutral, and that is the whole reason it is here rather than inside the sky
    /// manager that used to own it.** Every one of these is an `osgParticle` system — the rain one
    /// built here and the rest loaded out of a NIF the weather names — and a particle system is a
    /// thing both renderers read. The rasterizer draws it as quads; the ray tracer finds the same
    /// systems in the graph and marches its sprite layer against them. There is one of each, built
    /// once, and neither renderer is the one that owns them.
    ///
    /// What is *not* here is everything about drawing them. The occlusion pass that keeps rain off
    /// the inside of a roof, the cull callback that hides the lot of it under water, the shader
    /// hints a generated pipeline reads — those are the rasterizer's and stay with it. `getRevision` is how
    /// it knows to put them back: the nodes below are torn down and rebuilt whenever the weather
    /// changes what falls, and whatever a renderer hung on them went with them.
    class Precipitation
    {
    public:
        /// @param parent where the nodes go. **Camera-relative in the rasterizer's graph**, because
        ///        the rain is a box that follows the eye rather than a place in the world.
        Precipitation(osg::Group* parent, Resource::SceneManager& scenes, osg::Camera* camera);
        ~Precipitation();

        /// What this weather drops, how hard, and how fast the wind drives it.
        void setWeather(const WeatherResult& weather);

        /// Turns the storm's own effect to face where it is driving.
        void update(float duration);

        /// Where the storm drives what it carries, which is what that effect is turned by.
        void setStormDirection(const osg::Vec3f& direction) { mStormDirection = direction; }

        /// Everything it has built, for whoever is drawing.
        osg::Group* getNode() { return mNode.get(); }

        /// Bumped whenever what is under `getNode` is torn down and built again.
        ///
        /// **A renderer that decorated those nodes has to do it again**, because what it hung on
        /// them was thrown away with them. Nothing here knows what those decorations are.
        unsigned int getRevision() const { return mRevision; }

        /// The two subtrees, or null where this weather has neither. A renderer decorating them
        /// wants these rather than a walk of `getNode`, since only these two are its business.
        osg::Group* getRainNode() { return mRainNode.get(); }
        osg::PositionAttitudeTransform* getEffectNode() { return mParticleNode.get(); }

        bool hasRain() const { return mRainNode != nullptr; }

        /// Whether what is falling is the sort that dimples water. Rain and snow each carry their
        /// own answer in the content files and the rest of the weathers have none.
        bool ripplesEnabled() const;

        float getPrecipitationAlpha() const { return mPrecipitationAlpha; }
        float getBaseWindSpeed() const { return mBaseWindSpeed; }

        /// Where the water in this cell stands, and whether there is any.
        void setWaterLevel(float level) { mWaterLevel = level; }
        void setWaterEnabled(bool enabled) { mWaterEnabled = enabled; }

        /// Whether the eye is below the water, which is what stops the drops and hides them.
        ///
        /// **Asked of the camera and not of a cull traversal.** The rasterizer's
        /// `UnderwaterSwitchCallback` catches the eye point on its way past, which is the right
        /// answer for the subgraph it hides and an answer only a renderer that culls ever gets: the
        /// ray tracer never runs it, so what it last saw is the origin and the rain fell all the way
        /// to the sea bed. A camera is a thing both renderers keep pointed at the player.
        bool isUnderwater() const;

        /// How far a particle travels before the wrap carries it back — the box an occlusion pass
        /// has to cover, which is why anything outside asks.
        osg::Vec3f getWrapRange() const;

        /// Whether what is falling is the sort worth occluding: rain, and Solstheim's snow. Ash and
        /// blight blow through a roof in the original too.
        bool wantsOcclusion() const;

    private:
        void createRain();
        void destroyRain();
        void updateRainParameters();

        Resource::SceneManager& mSceneManager;
        osg::Camera* mCamera;

        osg::ref_ptr<osg::Group> mNode;

        osg::ref_ptr<osg::Group> mRainNode;
        osg::ref_ptr<osgParticle::ParticleSystem> mRainParticleSystem;
        osg::ref_ptr<osgParticle::BoxPlacer> mPlacer;
        osg::ref_ptr<RainCounter> mCounter;
        osg::ref_ptr<RainShooter> mRainShooter;

        osg::ref_ptr<osg::PositionAttitudeTransform> mParticleNode;
        osg::ref_ptr<osg::Node> mParticleEffect;
        VFS::Path::Normalized mCurrentParticleEffect;

        std::string mRainEffect;
        float mRainSpeed = 0.f;
        float mRainDiameter = 0.f;
        float mRainMinHeight = 0.f;
        float mRainMaxHeight = 0.f;
        float mRainEntranceSpeed = 1.f;
        int mRainMaxRaindrops = 0;

        bool mRainRipplesEnabled;
        bool mSnowRipplesEnabled;

        float mPrecipitationAlpha = 0.f;
        float mWindSpeed = 0.f;
        float mBaseWindSpeed = 0.f;

        bool mIsStorm = false;
        osg::Vec3f mStormDirection;

        float mWaterLevel = 0.f;

        /// True to start with, which is what the rasterizer's `UnderwaterSwitchCallback` assumes
        /// until a cell says otherwise.
        bool mWaterEnabled = true;

        unsigned int mRevision = 0;
    };
}

#endif
