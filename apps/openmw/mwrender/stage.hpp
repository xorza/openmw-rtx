#ifndef GAME_RENDER_STAGE_H
#define GAME_RENDER_STAGE_H

#include <osg/ref_ptr>

namespace osg
{
    class Camera;
    class FrameStamp;
    class Group;
    class Stats;
}

namespace osgGA
{
    class EventQueue;
}

namespace osgUtil
{
    class IncrementalCompileOperation;
    class UpdateVisitor;
}

namespace osgViewer
{
    class ScreenCaptureHandler;
    class Viewer;
}

namespace MWRender
{
    /// The frame, the eye and the input queue — the parts of a viewer that are not graphics.
    ///
    /// **`osgViewer::Viewer` is two things and thirteen classes wanted the smaller one.** It bundles
    /// the frame stamp, the master camera, the event queue and the update traversal — none of which
    /// touches OpenGL — with a graphics context, a threading model and a draw dispatcher. Everything
    /// from the GUI to the input wrapper had to import `osgViewer` to reach the first half, and that
    /// is a good part of why the renderer looked unswappable. `Stage` is the first half, named.
    ///
    /// **A facade over the viewer rather than an owner of what it holds.** The camera, the frame
    /// stamp and the event queue stay the viewer's own objects at this step: `osgViewer::View`
    /// gives its default camera a back-buffer draw and read target that the screen capture depends
    /// on, and `Viewer::advance` writes frame timings into the viewer's stats as it stamps the
    /// frame. Substituting objects the viewer did not make would change the picture, and this step
    /// must not. Ownership moves the other way at step 3, when `GlRenderer` takes the viewer and is
    /// handed a stage that already exists (`docs/rtx/renderers.md` §7).
    class Stage
    {
    public:
        explicit Stage(osgViewer::Viewer& viewer);
        ~Stage();

        Stage(const Stage&) = delete;
        Stage& operator=(const Stage&) = delete;

        /// View, projection, viewport and cull mask. Whether there is a graphics context behind it
        /// is the renderer's business, and under a renderer that owns its own surface there is none.
        osg::Camera& getCamera() const;

        /// Frame number, simulation time and reference time, advanced once per frame.
        osg::FrameStamp& getFrameStamp() const;

        /// Where SDL puts what it read, and where the scene graph's handlers read it from.
        osgGA::EventQueue& getEvents() const;

        osgUtil::UpdateVisitor& getUpdateVisitor() const;

        /// Per-frame counters, keyed by frame number. Every subsystem reports into this one.
        osg::Stats& getStats() const;

        /// Whatever is topmost. Not the node the engine created: the rasterizer inserts its
        /// post-processing group above the world, and the render-to-texture cameras the GUI hangs
        /// off the top have to land above that too, so this asks rather than remembers. A group
        /// rather than a node so that asking is always answerable.
        osg::Group& getSceneRoot() const;
        void setSceneRoot(osg::Group& root);

        void advance(double simulationTime);
        void eventTraversal();
        void updateTraversal();

        /// Draws the frame. Called by the main loop, and again by the four places that get a frame
        /// onto the screen from inside one — the loading screen, a modal message box, a video and
        /// the screenshot. Each sequences the traversals itself rather than share one entry point,
        /// because each interleaves different work between them.
        ///
        /// This and everything under it becomes `MWRender::Renderer` at step 3. It is here now
        /// because `Stage` is where `osgViewer` stopped being everyone's business, and a call has
        /// to go somewhere before the interface it belongs to exists.
        void renderTraversals();

        /// Stops and restarts the draw threads, so the graph can be mutated with nothing reading
        /// it. A renderer that draws on the calling thread answers both with nothing.
        void suspendDraw();
        void resumeDraw();

        /// Compiles arriving resources over several frames instead of stalling on first use.
        /// Null where `OPENMW_DONT_PRECOMPILE` asked for none, which is why this one is a pointer.
        osgUtil::IncrementalCompileOperation* getCompileOperation() const;
        void setCompileOperation(osgUtil::IncrementalCompileOperation* operation);

        /// The screenshot key's route to the frame buffer, installed by the engine once the work
        /// queue that writes the file exists.
        void setScreenCapture(osgViewer::ScreenCaptureHandler& handler);
        void captureNextFrame();

        /// The rasterizer's viewer, for the three component classes that still take one: MyGUI's
        /// render manager, the video wrapper's vsync and the shader hot-reloader. All three are the
        /// OpenGL renderer's, and this accessor is deleted with the step that moves them.
        osgViewer::Viewer& getViewer() const { return mViewer; }

    private:
        osgViewer::Viewer& mViewer;
        osg::ref_ptr<osgViewer::ScreenCaptureHandler> mScreenCapture;
    };
}

#endif
