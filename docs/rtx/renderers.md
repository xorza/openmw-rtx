# Renderers, and what upstream has to stop knowing

An earlier draft of this document optimised for *minimal changes in upstream code* and reached a
design built around leaving things where they are — a headless `osgViewer::Viewer`, a display class
bolted beside `RenderingManager`, OpenGL semantics left in the content model and decoded on the way
out. **That brief is withdrawn.** The priority is a clean long-term structure with no shortcuts, and
room for more than two renderers.

That is a different question and it has a different answer, because the thing standing between this
tree and a third renderer is not the window, the context or the frame loop. Those are small and they
are already understood. It is that **OpenGL is the content model**: the loaders know a texture is a
glow map and record that as "a texture unit named `emissiveMap` on an `osg::StateSet`", and every
renderer that is not OpenGL has to read it back out.

This document is the investigation and the architecture that follows from it. It does not overlap
`backends.md`, which is Vulkan against Metal *inside* the ray tracer — a level below the `Renderer`
described here.

---

## 1. Where OpenGL actually is

Measured over `apps/openmw` and `components`, not estimated.

**Direct GL is small, and nearly all of it is inside features one renderer owns.** Twenty files
contain a `gl*()` call at all, and the busiest are `myguirendermanager` (13), `stereo/multiview`
(12), `pingpongcanvas` (11), `gldebug` (5), `ripples` (5), `distortion` (5), `compositemaprenderer`
(4), `debugdraw` (4), `luminancecalculator` (4). The most-used entry points are `glClear`,
`glClearColor`, `glBlitFramebuffer`, `glBindFramebuffer`, `glViewport`, `glBindTexture`,
`glUniform3f`, `glMemoryBarrier`, `glGetString`, `glGenerateMipmap` — a post-processing chain, a
compute dispatch and a debug overlay. **None of it is load-bearing for the game.**

**`GL_` constants are three different things wearing one prefix.** Of roughly 600 occurrences:

- *Pixel formats* — `GL_RGB` (34), `GL_RGBA` (31), `GL_UNSIGNED_BYTE` (27), `GL_RGBA16F`,
  `GL_DEPTH24_STENCIL8`, the S3TC names. A format vocabulary every renderer needs, which only
  happens to be spelled in GL.
- *Fixed-function modes* — `GL_CULL_FACE` (21), `GL_BLEND` (18), `GL_ALWAYS` (16), `GL_NEVER` (15),
  `GL_DEPTH_TEST` (12), `GL_ALPHA_TEST` (8). **Material semantics encoded as pipeline state.** These
  are the problem.
- *Device* — buffer bits, extension strings, `GL_APIENTRY`.

**`mwrender` is two things wearing one name.** 23,102 lines:

| | lines | what it is |
|---|---:|---|
| game and content | 9,903 | `animation`, `npcanimation`, `objects`, `camera`, `objectpaging`, `groundcover`, `effectmanager`, `fogmanager`, `terrainstorage`, `landmanager`, the animation controllers |
| **the OpenGL renderer** | **6,985** | `postprocessor`, `sky`, `skyutil`, `water`, `ripples`, `ripplesimulation`, `precipitationocclusion`, `transparentpass`, `distortion`, `pingpongcanvas`, `pingpongcull`, `luminancecalculator`, `screenshotmanager` |
| game features built as render-to-texture | 2,445 | `characterpreview`, `localmap`, `globalmap` |
| debug overlays | 1,345 | `navmesh`, `actorspaths`, `recastmesh`, `pathgrid`, `bulletdebugdraw` |
| the god-object | 2,184 | `renderingmanager` |

**`components/sceneutil` splits almost exactly in half.** 16,358 lines: **7,568 are the OpenGL
renderer** — `mwshadowtechnique` alone is 3,436, plus `shadow`, `shadowsbin`, `rtt`, `depth`,
`stateupdater`, `color`, the clustered `lightmanager` — against 7,614 of content and graph machinery:
`riggeometry`, `morphgeometry`, `skeleton`, the controllers, `attach`, `clone`, `optimizer`,
`workqueue`, the light data. Add `components/fx` (3,848 — the post-processing language),
`components/stereo` (2,064) and half of `components/shader` (2,062).

**So roughly 22,000 lines of OpenGL renderer live in directories named after the engine, interleaved
with the game logic they serve.** The first move toward more than one renderer is not an interface.
It is a directory: gather them, and the seam becomes visible because it becomes a link boundary.

## 2. The five patterns, and the agnostic form of each

### Pattern 1 — material semantics encoded as GL state. *This is the one that matters.*

`NifOsg::handleTextureProperty` (`nifloader.cpp:2153`) knows exactly what each texture is —
`BaseTexture`, `GlowTexture`, `DarkTexture`, `BumpTexture`, `DetailTexture`, `DecalTexture`,
`GlossTexture` — and records that knowledge by **naming a texture unit on an `osg::StateSet`**. It
puts `bumpMapMatrix` and `envMapLumaBias` on as `osg::Uniform`s. `NiAlphaProperty` becomes an
`osg::BlendFunc` and an `osg::AlphaFunc`; `NiStencilProperty` becomes a `GL_CULL_FACE` mode;
`NiMaterialProperty` becomes an `osg::Material`.

`Shader::ShaderVisitor` then re-reads those unit names, discovers `_n` and `_spec` siblings by
filename convention (`shadervisitor.cpp:255-368`), and writes *more* named units back.

And `RtxBridge::SceneExtractor::readMaterial` (`sceneextractor.cpp:1058`) reads the names back out,
plus `osg::AlphaFunc`'s reference value, whether a `BlendFunc` is present, whether `GL_CULL_FACE` is
on, `osg::Material`'s diffuse and emission, and the `emissiveMult` uniform — and reconstructs the
material the loader had in its hands three steps earlier.

**Every fact in that round trip was known at load time and thrown into an OpenGL container.** A
second non-GL renderer writes that decoder again; a third writes it a third time. This is what
"upstream is not renderer-agnostic" means concretely, and it has nothing to do with contexts or
`#ifdef`s.

It also explains an open issue that looked like a ray-tracing defect — *"a material carries no
texture transform, so a surface whose shading animates by scrolling its UVs stands still"*.
`NifOsg::UVController` writes an `osg::TexMat`. There is nowhere for a texture transform to go
because **there is no material type**, only a state set, and the decoder was never taught that
attribute. It could not have been taught it cleanly: the decoder is guessing at semantics from
pipeline state, and a `TexMat` is exactly as ambiguous as everything else it guesses at.

> **Agnostic form.** An explicit `MWRender::Material`, authored where the information exists — the
> NIF loader, `Terrain`, the ESM4 loader, and the map-discovery half of the shader visitor — and
> carried on the drawable. Texture roles as an enum, not a string. Alpha mode, alpha reference,
> two-sidedness, the colours, the UV transform, the emissive multiplier as fields. The OpenGL
> renderer compiles a state set from it; every other renderer reads it directly.

### Pattern 2 — shaders as the only material implementation

`Shader::ShaderManager` + `files/shaders` + `osg::Program` on state sets: GLSL, specific to one
renderer. But the shader *visitor* is doing two jobs at once — choosing a program, and **discovering
material facts**: auto normal maps, auto specular maps, whether a surface is alpha-tested, whether it
is a particle system. The harness needs it to run for the second job and takes the first as baggage;
`apps/rtxtool/world.cpp:150` says so in a comment, and adds that it throws when a program will not
build, taking the model down with it.

> **Agnostic form.** Split it. Map discovery and material classification move into the material
> authoring pass and run for every renderer; program selection stays in the OpenGL renderer and runs
> for nobody else.

### Pattern 3 — render-to-texture, for two unrelated reasons

`SceneUtil::RTTNode` and raw `osg::Camera` RTT have ten users and they divide cleanly:

- **Rasterizer implementation** — shadow maps, water reflection and refraction, the sky RTT,
  precipitation occlusion, terrain composite maps, the post-processing ping-pong, luminance
  reduction. A ray tracer does not replace these; it makes the question not arise.
- **Game features** — the inventory doll, the race-creation preview, the local map's tiles, the
  global map's cells. The game genuinely wants "an image of this content from that camera", and
  every renderer owes it one.

> **Agnostic form.** The first group is private to the OpenGL renderer and moves with it. The second
> is one interface with three methods, which a ray tracer answers with a trace — something
> `openmw-rtxtool shot` has done since M0.

### Pattern 4 — the frame graph: render bins, cull-time binning, draw callbacks

`MWRender::RenderBins` orders sky, water, depth-sorted, first-person, sun glare and distortion.
`Terrain::TerrainDrawable::cull` bins the chunk with `cv->addDrawableAndDepth` and never applies it.
`SceneUtil::StateSetUpdater` as a cull callback produces state that exists only during cull.
`osg::Drawable::drawImplementation` and `osg::Camera::DrawCallback` reach the driver directly.

This is pure rasterizer: bin ordering exists because a rasterizer cannot sort transparency, and
cull-time work exists because a rasterizer's frustum is a budget. `CLAUDE.md` already says these do
not come across.

> **Agnostic form.** None needed — the layer above must simply stop depending on it. Two places do,
> and both are known: terrain LOD is computed only for a `CullVisitor` (§5), and animated state sets
> existed only during cull until the mirror learned to run the animators itself.

### Pattern 5 — the device

The context, the extensions (`SceneUtil::getGLExtensions`), the maximum texture units, the swap,
vsync, the graphics window, `osgViewer`'s threading model. About forty call sites, all in
`engine.cpp`, `components/sdlutil` and the realize operations.

> **Agnostic form.** A capability struct the renderer answers, and a surface the renderer owns. This
> is the easy pattern, and the one the withdrawn draft mistook for the whole problem.

## 3. The architecture

Four layers. The abstract class sits at layer 3, and the reason the earlier version was not enough is
that it had no layer 2.

```
1  Game        MWWorld, MWMechanics, MWScript, MWGui      no rendering vocabulary at all
2  Scene       the OSG graph, plus Material / Light /     what exists, described
               Environment described on it
3  Renderer    MWRender::Renderer  (abstract)             what it looks like
4  Backend     GL · Vulkan-RT · Metal-RT · future         how a device does it
```

**OSG stays, and that is not a compromise.** A tree of groups, transforms and geometry holding arrays
of positions, normals, UVs and indices is a content model, not a renderer. What leaves it is *OpenGL
semantics*: pipeline state standing in for material properties, GLSL programs, render bins, cull-time
side effects. After that the graph describes the world and a renderer decides what to do about it —
which is what `RtxBridge::SceneExtractor` already does, minus the decoding.

### Layer 3 — the base class

Derived from what the game actually asks for, which is a far shorter list than `RenderingManager`'s
eighty-five methods.

```cpp
// apps/openmw/mwrender/renderer.hpp

namespace MWRender
{
    /// What this renderer can do, so nothing above it has to assume.
    ///
    /// **Capabilities rather than null returns.** A third renderer will not have some of what the
    /// rasterizer has, and the difference has to be answerable *before* a settings page offers a
    /// slider for it or a Lua script calls into it. A ray tracer answering `mShadowMaps = false` is
    /// not reporting a gap: it has shadows and no maps, and that is the correct answer.
    struct Capabilities
    {
        int mTextureUnits = 0;
        int mMaxTextureSize = 0;
        bool mCompressedTextures = false;

        bool mPostProcessing = false;
        bool mShadowMaps = false;
        bool mStereo = false;
        bool mDebugOverlays = false;
    };

    /// What there is to draw and what light is on it. No renderer appears in this type.
    struct SceneFrame
    {
        osg::Node& mRoot;
        const osg::Camera& mCamera;
        const osg::FrameStamp& mWhen;

        /// Sun direction and irradiance, ambient, fog colour and extinction, sky zenith, water
        /// level — assembled by `RenderingManager` from where every route to them has already met.
        /// `MWRender::Lighting`, moved up out of the tracer because it describes the world rather
        /// than the renderer. Not `Environment`, which `MWBase` already has.
        const Lighting& mLighting;
    };

    /// One image of the world, whichever renderer makes it.
    ///
    /// Nothing below this line is abstracted. Contexts, swapchains, command buffers, framebuffers,
    /// render bins, descriptor sets and acceleration structures belong to a renderer outright — an
    /// interface over those would be a mini-GL that Vulkan does not fit, which is the argument
    /// `backends.md` §3 makes one level further down for the same reason.
    class Renderer
    {
    public:
        virtual ~Renderer() = default;

        virtual const Capabilities& getCapabilities() const = 0;

        /// One frame on the screen: the world where there is one, and the GUI over it. Null while
        /// the menu, the loading screen or a video is up and no world exists yet.
        virtual void renderFrame(const SceneFrame* frame) = 0;

        /// The frame without the GUI, for the screenshot key and for save thumbnails.
        virtual void capture(osg::Image& image, int width, int height) = 0;

        /// An image of the world from somewhere that is not the eye: the inventory doll, the
        /// race-creation preview, a local map tile, a global map cell.
        virtual std::unique_ptr<OffscreenView> createOffscreenView(const OffscreenViewSpec& spec) = 0;

        /// The MyGUI backend. `MyGUI::RenderManager` is MyGUI's own interface, so this is a second
        /// implementation of an existing one rather than a new abstraction.
        virtual std::unique_ptr<GuiRenderer> createGuiRenderer(const GuiRendererSpec& spec) = 0;

        virtual void resized(int width, int height) = 0;
        virtual void setVSync(SDLUtil::VSyncMode mode) = 0;
        virtual void processChangedSettings(const Settings::CategorySettingVector& changed) = 0;

        /// Null unless `Capabilities::mPostProcessing`. Eleven Lua bindings and the HUD gate on it.
        virtual PostProcessor* getPostProcessor() { return nullptr; }

        /// Null unless `Capabilities::mDebugOverlays`. The navmesh, pathgrid and Bullet drawers.
        virtual Debug::DebugDrawer* getDebugDrawer() { return nullptr; }
    };

    /// The one place the choice is made — a registry rather than a hard-coded pair, because "there
    /// could be more renderers" is the requirement this whole design exists to serve.
    std::unique_ptr<Renderer> createRenderer(std::string_view name, const RendererSpec& spec);
}
```

`RenderingManager` stays concrete and stays the world-facing god-object. Its eighty-five methods stay
non-virtual, because they are `addCell`, `moveObject`, `castRay`, `getHalfExtents` and paging — world
management, identical under every renderer because every renderer consumes the same described scene.
About fifteen of them delegate to `mRenderer`. That is the one conclusion the withdrawn draft reached
that the measurements did not move.

### Layer 2 — the scene

This is the new work, and it is the majority of it.

```cpp
// components/surface/material.hpp

enum class TextureRole { Diffuse, Normal, NormalHeight, Emissive, Specular,
                         Dark, Detail, Decal, Gloss, Bump, Environment };

enum class AlphaMode { Opaque, Cutout, Blend };

/// What a surface is, as the content said and before any renderer has an opinion.
struct Material
{
    /// One texture per role, null where the content has none.
    ///
    /// The `osg::Image` and not an `osg::Texture2D`: after load, `osgDB::SharedStateManager`
    /// canonicalises equal textures across every model in the cache, so the object the loader
    /// bound is replaced by one it never saw. The image survives that and carries the file name.
    std::array<osg::ref_ptr<osg::Image>, sTextureRoleCount> mTextures;

    AlphaMode mAlphaMode = AlphaMode::Opaque;
    float mAlphaRef = 0.0f;
    bool mTwoSided = false;

    osg::Vec4f mDiffuseColour{ 1, 1, 1, 1 };
    osg::Vec3f mAmbientColour{ 1, 1, 1 };
    osg::Vec3f mEmissiveColour{ 0, 0, 0 };
    osg::Vec3f mSpecularColour{ 0, 0, 0 };
    float mGlossiness = 0.0f;
    float mEmissiveMult = 1.0f;

    /// Scaled about the middle of the texture, then offset. `NifOsg::UVController` rewrites both
    /// every frame it is applied, exactly as it rewrites the `osg::TexMat` the rasterizer reads.
    osg::Vec2f mTextureScale{ 1, 1 };
    osg::Vec2f mTextureOffset;
};
```

Authored by `NifOsg`, `Terrain`, the ESM4 loader and the shader visitor's discovery half. Carried on
the drawable. Read by every renderer.

**How the state set stops being a duplicate, safely.** Two positions exist and only one is the end
state:

- **(a) authored alongside** — the loaders gain a material and keep building state sets exactly as
  now. Additive, zero risk to a twenty-year-old content pipeline, and `SceneExtractor`'s decoder
  deletes the day it lands.
- **(b) authored instead** — the material is the only authored form, and the OpenGL renderer compiles
  a state set from it. No duplication, and the loaders stop knowing what a `BlendFunc` is.

(b) is the destination and (a) is how it is reached without a flag day — **because with both in the
tree the equivalence is checkable.** A sweep walks every mesh in the VFS, builds the state set the
old way and the new way, and compares attribute by attribute. That is a proof over real content, it
runs headless in the harness, and it is what turns deleting the old path from a hope into a fact.
Nothing else in this migration has that property, which is why it is worth two steps rather than one.

### What replaces `osgViewer::Viewer`

Under the withdrawn brief the answer was "keep it, headless, and guard the three places that would
crash". Under this one it is not, because the viewer is `GlRenderer`'s and eleven other places should
not import it to get four things that are not graphics.

Thirteen classes hold or take an `osgViewer::Viewer`. Five — `PostProcessor`, `ScreenshotManager`,
`MyGUIPlatform::RenderManager`, `LoadingScreen`, `Stereo::Manager` — are the OpenGL renderer's and
move inside it. `ActionManager` uses it for exactly one line
(`mScreenCaptureHandler->captureNextFrame(*mViewer)`, `actionmanager.cpp:172`) and loses it entirely
to `Renderer::capture`. The remaining seven want the frame stamp, the master camera, the event queue
and the update traversal — none of which touches OpenGL.

```cpp
/// The frame, the eye and the input queue — the parts of a viewer that are not graphics.
class Stage
{
public:
    osg::Camera& getCamera() const;
    osg::FrameStamp& getFrameStamp() const;
    osgGA::EventQueue& getEvents() const;
    osgUtil::UpdateVisitor& getUpdateVisitor() const;
    osg::Stats& getStats() const;
    osg::Group& getSceneRoot() const;
    void setSceneRoot(osg::Node& root);

    void advance(double simulationTime);
    void eventTraversal();
    void updateTraversal();
};
```

`GlRenderer` constructs an `osgViewer::Viewer`, calls `setCamera(&stage.getCamera())` and
`setSceneData(root)`, and keeps every one of upstream's threading, realize and traversal decisions
untouched inside itself. Step 2 came to 227 lines of new code and eight changed signatures — three
of them classes listed above as `GlRenderer`'s, because taking a `Stage&` is the smallest change
that gets the include out and costs nothing at the step that moves them. That is what not taking
the shortcut costs here, and it is small.

**Two things step 2 measured that this sketch had wrong.**

*The stage cannot own those objects yet, only name them.* Substituting a camera the viewer did not
make loses the defaults `osgViewer::View`'s constructor gives it — `GL_BACK` as both draw and read
buffer, which is what the screen capture reads — and `Viewer::advance` writes frame duration and
frame rate into the viewer's own stats as it stamps the frame. So `Stage` reads all six through the
viewer it was handed, and ownership goes the other way at step 3, when the renderer is the thing
being constructed around a stage that already exists. `getSceneRoot()` asks rather than remembers
for a related reason: `PostProcessor` inserts itself above the world as the scene data, and the GUI's
render-to-texture cameras have to hang off whatever is topmost.

*Four operations on it are the renderer's, and they had nowhere else to go.* `renderTraversals`,
`suspendDraw`/`resumeDraw`, the incremental compile operation and `captureNextFrame` are
`MWRender::Renderer` methods that step 3 has not created yet; they sit on `Stage`, grouped and
labelled, and move together. So does `getViewer()`, which exists for the three *component* classes
that still take a viewer — MyGUI's render manager, the video wrapper's vsync and the shader
hot-reloader. That is the whole of what step 2 could not finish, and it is visible in one file
rather than spread over thirteen.

## 4. Every place OpenGL is reached

The work list. Nothing else in the tree touches a context.

| where | what it does | where it goes |
|---|---|---|
| `engine.cpp:535` | `SDL_WINDOW_OPENGL`, the `SDL_GL_SetAttribute` block, the AA retry loop | `GlRenderer`'s constructor |
| `engine.cpp:613` | `SDLUtil::GraphicsWindowSDL2` | `GlRenderer`; `components/sdlutil` keeps it and nothing else calls it |
| `engine.cpp:643` | `camera->setGraphicsContext(...)` | `GlRenderer`, on the `Stage`'s camera |
| `engine.cpp:646-716` | realize operations: identify, GL extensions, depth format, colour format, GL debug, stereo | `GlRenderer` |
| `engine.cpp:717` | `mViewer->realize()` | `GlRenderer` |
| `engine.cpp:718,769` | `mGlMaxTextureImageUnits` → `ShaderManager::setMaxTextureUnits` | `Capabilities::mTextureUnits` |
| `engine.cpp:835` | `SceneUtil::getGLExtensions()` | **dead already** — its only use is inside `#if OSG_VERSION_LESS_THAN(3,6,6)` and the call sits outside it. Move it in; a fix either way |
| `engine.cpp:361` | `mViewer->renderingTraversals()` | `Renderer::renderFrame` |
| `engine.cpp:786` | `AsyncScreenCaptureOperation`, `osgViewer::ScreenCaptureHandler` | `Renderer::capture` |
| `engine.cpp:755` | `Stereo::Manager` | `GlRenderer`; `Capabilities::mStereo` |
| `windowmanagerimp.cpp:214` | `MyGUIPlatform::Platform` — 1,529 lines of GL | `Renderer::createGuiRenderer` |
| `mwgui/*.cpp` ×11 | `MyGUIPlatform::OSGTexture` constructed directly | six are "a texture the CPU writes" (`MyGUI::RenderManager::createTexture`), four are `OffscreenView`s, one is the loading screen's framebuffer copy |
| `loadingscreen.cpp:353` | `renderingTraversals()` inside the load loop | `Renderer::renderFrame(nullptr)` |
| `loadingscreen.cpp:302` | `CopyFramebufferToTextureCallback` | a capability, or a flat colour |
| `renderingmanager.cpp:217` | `SceneUtil::LightManager` as the scene root | `GlRenderer`; the described scene carries lights, and `SceneUtil::Light` is already a plain struct rather than an `osg::Light` |
| `renderingmanager.cpp:249` | `SceneUtil::ShadowManager` | `GlRenderer` |
| `renderingmanager.cpp:332` | `PostProcessor` | `GlRenderer`; `Capabilities::mPostProcessing` |
| `renderingmanager.cpp:340` | `Water` | `GlRenderer`. The water *level* is world state and stays |
| `renderingmanager.cpp:372` | `SkyManager` | split: the dome is `GlRenderer`'s, the weather-derived getters are world state |
| `renderingmanager.cpp:345` | `ScreenshotManager` | `Renderer::capture` |
| `renderingmanager.cpp:321-327` | `StateUpdater`, `SharedUniformStateUpdater`, `PerViewUniformStateUpdater` | `GlRenderer` |
| `renderingmanager.cpp:295` | `IncrementalCompileOperation` | `GlRenderer` |
| `quadtreeworld.cpp:475` | terrain LOD computed only for a `CullVisitor` | §5 |
| `imagemanager.cpp:65` | S3TC support | `Capabilities::mCompressedTextures`; **already guarded** by `glExtensionsReady()` |
| `shadervisitor.cpp:634` | `isGpuShader4Supported` | the program-selection half, which is `GlRenderer`'s |
| `nifloader.cpp:2153,2479` | NIF properties → state-set attributes and named texture units | **pattern 1** — the material |

## 5. Terrain and paging, because a renderer need not cull

`Terrain::QuadTreeWorld::accept` returns immediately unless the visitor is a `CullVisitor` or an
`IntersectionVisitor` (`quadtreeworld.cpp:475`), and `Terrain::RootNode::accept` forwards to it
instead of to `Group::accept`. **A plain visitor sees no terrain and no paged objects at all** — the
chunks are never children, they are `ViewDataEntry::mRenderingNode`s accepted during the cull.

This does not bite today only because `distant terrain = false` is the default and `TerrainGrid`
attaches real children. With it on, the in-game mirror already loses the ground.

The by-distance path exists: `QuadTreeWorld::preload` (`quadtreeworld.cpp:546`) runs the same
`DefaultLodCallback` over the same quadtree from a view point with no visitor at all — that is how
the background cell preloader works. `accept` and `preload` differ in the `compile` flag and one
cull-only water-culling call.

So `QuadTreeWorld` gains a public `traverse(osg::NodeVisitor&, const osg::Vec3f& viewPoint)`, `accept`
becomes a three-line wrapper that pulls the view point off the cull visitor, and a renderer that does
not cull asks by distance instead. Object paging and groundcover come free — both are
`QuadTreeWorld::ChunkManager`s, so they are already inside `mRenderingNode`.

## 6. Where the code lives

Directories, because a library is a link boundary and a subdirectory is a convention.

```
components/surface/         what a surface is: Material, TextureRole, AlphaMode. Links no
                            graphics API. NifOsg, Terrain and the shader visitor author it.
                            Light and Environment join it as their steps land.
components/sceneutil/       content, graph machinery, and the GL half that other libraries and
                            OpenCS also link — see below
apps/openmw/mwrender/       game-facing: animation, objects, camera, paging, effects, fog,
                            RenderingManager, Stage, and renderer.hpp
apps/openmw/mwrender/gl/    the OpenGL renderer: sky, water, post-processing, ripples, the
                            ping-pong chain, osgViewer, the MyGUI backend, the screenshot
apps/openmw/mwrender/rtx/   the ray tracer, over components/rtx + rtxbackends + rtxbridge
```

**There is no `mwrender/metal/`, and `mwrender/rtx/` is not `mwrender/vulkan/`.** The API split
already happened one layer down: `Rtx::createRenderer` in `components/rtxbackends` picks
`components/rtxvulkan` or `components/rtxmetal` at compile time by what the machine is. Everything in
`mwrender/rtx/` — mirroring the graph, driving the frame, the extents, the capabilities — is written
once for both, so a second directory beside it would be a copy of all of it, which is the duplication
`backends.md` exists to prevent. The asymmetry with `mwrender/gl/` is real and correct: that renderer
*is* an API, down to its sky and its water, while this one had its API factored out into components
before the game ever saw it. The one thing that leaked back — which SDL flag a window needs before a
surface can be made on it — is `Rtx::surfaceWindowFlag`, answered by the same place that picks the
backend.

**`components/sceneutil`'s GL half cannot follow, and §1's line count was the wrong measurement.**
Counting the lines said 7,568 belong to the rasterizer; counting the *consumers* says they cannot
leave. `apps/opencs` links `shadow`, `stateupdater` and `lightmanager`; `components/terrain` links
`lightmanager` and `depth`; `components/fx` links `depth`, `color` and `lightmanager`;
`components/stereo` links `mwshadowtechnique`; `components/resource`, `components/nifosg`,
`apps/rtxtool` and `apps/components_tests` each link some of it. Moving them under
`apps/openmw/mwrender/gl/` would break the editor and three component libraries. So they stay a
shared library, and the link boundary step 3 draws is the `mwrender` half — which is the half the
game reaches through `Renderer` anyway.

`components/surface` is the load-bearing one. It is what makes a fourth renderer a new directory
rather than a new decoder, and it is where `Rtx::Material` and the bridge's `readMaterial` converge:
the bridge stops reverse-engineering and starts copying.

## 7. Implementation steps

Ordered so each lands, builds and is checkable, and so the OpenGL path is moved rather than changed.
**Steps 0–3 and 5 are worth doing whether or not a ray tracer ever ships**: they are what makes the
renderer a component instead of the engine.

### Step 0 — guards and dead code — **done**

Move `engine.cpp:835`'s `getGLExtensions()` inside its `#if`. Null-guard `sdlinputwrapper.cpp:271`.

*Verified by*: the game plays identically; both test binaries pass.

### Step 1 — `Surface::Material`, authored alongside — **done**

`components/surface` with `TextureRole`, `AlphaMode` and `Material`. `NifOsg` authors one for every
shape it builds, inheriting it down its own node recursion the way a NIF property inherits;
`Terrain` authors one per layer pass; `Shader::ShaderVisitor` adds the maps it discovers by
filename. `NifOsg`'s three state-set controllers — colour, alpha and flip — animate the description
alongside the `osg::Material` and the texture unit they already animated, so a surface that changes
still changes for a renderer that reads the description.

`SceneExtractor::readMaterial` and its five decode helpers deleted; the extractor counts surfaces
nothing described instead of guessing at them, and that count is zero across every view.

Two things the sweep found that no amount of reading would have: `osgDB::SharedStateManager`
canonicalises texture objects across models after load, so the description holds the `osg::Image`
rather than the `osg::Texture2D` it was bound as; and a shallow-copied state set shares its whole
user-data container, so a controller writing a material would have written it into the node it was
copied from.

*Verified by*: five golden images byte-identical; `apps/components_tests/rtxtool/material.cpp`
sweeping an exterior and the densest interior and asserting that every surface is described and
every description agrees with the pipeline state beside it — which is the equivalence test step 5
inverts.

### Step 2 — `MWRender::Stage`, and `osgViewer` becomes one renderer's business — **done**

Eight signatures change from `osgViewer::Viewer*` to `Stage&` — `RenderingManager`, `PostProcessor`,
`ScreenshotManager`, `LoadingScreen`, `WindowManager`, `InputManager`, `ActionManager` and
`World::init` — and `ActionManager` loses the screen-capture handler with it. `SDLUtil::InputWrapper`
narrows to the two objects it actually reads, an `osg::Camera&` and an `osgGA::EventQueue&`, because
a component should not learn a game-side type to stop importing `osgViewer`.

Six things hold or take an `osgViewer::Viewer` now rather than thirteen: `Engine`, which makes it;
`Stage`, which is the seam; and the four in `components` that are the OpenGL renderer's —
`MyGUIPlatform::RenderManager`, `SDLUtil::VideoWrapper`, `Stereo::Manager` and
`ShaderManager::update`. `mwgui`, `mwinput`, `mwworld` and every file in `mwrender` but `stage.cpp`
stopped naming it at all.

Two lines moved rather than changed: `setLightingMode(NO_LIGHT)` from `RenderingManager`'s
constructor to beside the viewer's other configuration in `Engine`, and the screen-capture handler
from an `Engine` member to `Stage`, which is where `Renderer::capture` will find it.

*Verified by*: five golden images byte-identical; both test binaries pass; the game reaches Seyda
Neen from `--skip-menu --new-game`, loads, traces, and quits clean with nothing new in the log —
which exercises the loading screen's own frame loop, the GUI, the input wrapper and the stage's
traversals, none of which any test binary reaches. The screenshot key and the stats overlay are
rewired but only a key press runs them; both are one forwarded call.

### Step 3 — `MWRender::Renderer`, with only `GlRenderer` behind it — **done**

`MWRender::Renderer` is nineteen methods and none is called more than a few times a frame:
`setSceneRoot`, the four that make a frame, `done`, the two captures, the draw suspension, the
compile operation, vsync, the shader hot-reload, the MyGUI platform, and this renderer's own
instrumentation. `GlRenderer` is the only implementation and `Engine` asks for it by name —
`createRenderer("opengl", spec)`, throwing where there is no such renderer rather than falling back
to one that would answer "why does it look like that" with silence.

`GlRenderer` now owns what `Engine` used to: the SDL window and its GL attributes, the antialiasing
retry loop, `GraphicsWindowSDL2`, every realize operation, `Stereo::Manager`, `realize()`, the
screen-capture chain, `ScreenshotManager`, and the stats overlay. **`Engine` names no OpenGL and no
`osgViewer` at all**, and neither does `mwgui`, `mwinput`, `mwworld` or anything in `mwrender`
outside `gl/`. The thirteen classes that held a viewer at the start of step 2 are down to one.

`Stage` stopped being a facade. It holds the camera, frame stamp, event queue, update visitor and
stats, and the renderer says which objects those are as it is constructed — `Stage::adopt`. That is
what step 2 could not do: substituting objects the viewer did not make loses its wiring, because the
update and event visitors hold the frame stamp. Taking what the renderer already made costs a
renderer built on `osgViewer` nothing and costs one that owns its surface a constructor call.

Thirteen files moved to `mwrender/gl/` — `postprocessor`, `sky`, `skyutil`, `water`, `ripples`,
`ripplesimulation`, `precipitationocclusion`, `transparentpass`, `distortion`, `pingpongcanvas`,
`pingpongcull`, `luminancecalculator`, `screenshotmanager` — with no line of GL changed, only
addresses and the four signatures that took a `Renderer&`.

**Two things this step measured that the plan above had wrong.** `components/sceneutil`'s GL half
cannot move (§6). And **`PostProcessor` cannot change owner here**: it holds `RenderingManager&` and
calls back into it in nine places while `RenderingManager` reaches into its `StateUpdater` in twenty,
and `RenderingManager` is built long after the window. Its address moved; who constructs it did not,
and untangling that is content work rather than a move.

**What is deferred, and why.** `Capabilities` carries one field — the texture-unit count, which the
shader manager reserves out of. The four booleans of §8 and the twenty-two null checks they gate are
not here: with one renderer behind the interface no consumer can take the false branch, so it would
be code nothing can run. They arrive with the renderer that answers false. `OffscreenView` likewise
waits for step 6, which is the step that has a second implementation of it; the GUI's route through
the renderer exists today as `createGuiPlatform`, which is enough to get `osgViewer` out of `mwgui`.

*Verified by*: five golden images byte-identical; both test binaries pass; the game reaches Seyda
Neen from `--skip-menu --new-game`, loads, traces and runs clean; and a second tree configured
`-DOPENMW_RTX=OFF` builds and links, which is the guarantee that the path not taken is still there.
Not verified beyond compiling: the settings pages and the Lua bindings, which reach the same
`RenderingManager::getPostProcessor()` they always did.

### Step 4 — `RtxRenderer` presents the world — **done**

`MWRender::Rtx::RtxRenderer` is a `Renderer` like any other: an SDL window created with
`SDL_WINDOW_VULKAN` and not one GL attribute set, a `Rtx::Renderer` handed that window so the backend
builds its own surface, and `renderFrame` that mirrors the graph, traces it and presents — in that
order, in one frame. `components/rtxgl`, `MWRender::Rtx::Composite` and `MWRender::Rtx::Tracer` are
gone, along with the interop test that was the only other thing that used them; `RenderingManager`
owns no tracer and knows no ray tracer at all. `Engine` asks for `"raytrace"` or `"opengl"` by name
from `[RTX] enabled` and throws where the one asked for will not start.

**It drives the frame itself, and that is eighty lines.** `advance` stamps the frame and writes the
two attributes the profiler reports against; `eventTraversal` drains what SDL queued and drops it,
because the function keys it carries are read by `osgViewer` handlers this renderer does not have and
everything the game acts on came through `SDLUtil::InputWrapper` long before; `updateTraversal` runs
the update visitor over the scene root **and over the master camera**, which is not in the graph and
is where `MWRender::Camera` puts the eye. OpenMW registers no update operations, sets no camera
manipulator and never gives `osgDB::Registry` a shared-state manager, so nothing else
`osgViewer::Scene::updateSceneGraph` does around the visitor applies.

**The GUI needed no null backend.** MyGUI's frame events come off an *update* callback and its draw
calls off a *cull* callback. A renderer that runs the first traversal and not the second gets a GUI
that lays out, animates and answers input while never reaching a draw — which is exactly "the menu
and the loading screen draw nothing". `MyGUIPlatform::RenderManager` used its `osgViewer::Viewer` for
one line, the initial viewport, so it takes an `osg::Camera` now and the same backend serves both
renderers. `components/myguiplatform` names no `osgViewer` at all.

**What had to be gated, and it was not speculative after all.** `PostProcessor`'s constructor reads
`GLExtensions` off the camera's graphics context, so under this renderer it is not a feature to
switch off but a null dereference — hence `Capabilities::mPostProcessing` and `RenderingManager`
building the chain only where there is one.

**The chain belongs to the renderer, and getting there took three wrong turns.** Guarding each of a
dozen uniform-block setters put the renderer's internals in every one of them; a standalone block for
the renderer with no chain was an object written to and sampled by nothing; gathering the writes into
one guarded call was better but still had `RenderingManager` deciding whether a shader chain should
exist. It does not decide. `GlRenderer` owns the `PostProcessor`, `Renderer::getPostProcessor()`
answers null where there is none — which is what §3 sketched all along — and
`Renderer::attachWorld` is the second phase where a renderer builds whatever it puts between the
world and the screen. `RenderingManager` has no `mPostProcessor` and no capability conditional left.

The coupling that made this look impossible at step 3 turned out not to exist: everything
`PostProcessor` wants from `RenderingManager` — the resource system, the sky, the light root, the
projection and screen-resolution callbacks — is already a public accessor. It needed a
`RenderingManager&`, never an owner. The only real constraint was ordering, and that is what
`attachWorld` is for: the renderer is made before there is a world, so what needs the world is built
when there is one.

**And then there was one channel.** Moving the chain exposed what the conditional had been hiding:
`RenderingManager` was answering the same twenty questions twice a frame in two directions — pushing
a `Lighting` down inside `SceneFrame` for the ray tracer, and being called back up by the rasterizer
to fill a uniform block. Same sun, same fog, same water, asked through two channels pointing opposite
ways. `SceneFrame` now carries one `WorldState` in the world's own undecoded numbers;
`PostProcessor::describe` spells it the way its shaders sample it, and `RtxRenderer` decodes it for a
linear transport. Nothing calls upward per frame, and the two the world kept nowhere but the block —
where the sun is drawn, and how visible it is — became members, which is what they always were.

The measure of it: `renderingmanager.cpp` and its header now contain no `#ifdef OPENMW_RTX` at all.
The god-object does not know the ray tracer exists.

`Capabilities::mPostProcessing` did not survive either. The one consumer left was the GUI deciding
whether to build the post-processing HUD, and the GUI is built before the world — so it could not ask
for a chain that did not exist yet. But the HUD reaches for one only when it is opened, and its
constructor is widgets and nothing else: build it always, and let `togglePostProcessorHud` find null
and decline. Every gate that lived on `Capabilities` turned out to be a null check wearing a hat.
What is left of the struct is the one thing nothing can be asked for — the texture-unit count, which
is a number rather than a thing.
Outside `mwrender`: the two `setExteriorFlag` calls on every cell change, the HUD and its key, the
Lua bindings — which raise a clear error rather than dereferencing null — and `Stereo::Manager`,
whose singleton two places reached for whether or not stereo was on. Three device queries that ran
with no context had to ask `SceneUtil::glExtensionsReady()` first, the way `imagemanager.cpp` already
did; the `OSG < 3.6.6` framebuffer workaround moved out of `Engine` into `GlRenderer`, where the
context it needs exists.

**Terrain by view point did not turn out to be needed here.** §5 is right that a plain visitor sees
nothing of `Terrain::QuadTreeWorld`, but that is the `distant terrain` path, and it is off by default
— `TerrainGrid` attaches real children and the mirror walks them like anything else. An exterior
draws: 10,676 instances from 1,031 meshes in the Ascadian Isles, with terrain, trees, buildings and
water in the frame. §5 stays on the list for the day distant terrain is turned on, which is where it
always belonged.

**Still open**, both in `.notes/ISSUES.md`: DLSS Ray Reconstruction fails on this path with
`NVSDK_NGX_Result_FAIL_NotInitialized` on the first evaluate — it works in the harness at the same
extent on the same device and worked in the game before it owned its window — so `upscale` has to be
`off`. And `RtxRenderer::saveScreenshot` is a stub; `OPENMW_RTX_SHOT` still writes frames.

*Verified by*: **the picture** — a loaded save in the Ascadian Isles, written out through
`OPENMW_RTX_SHOT`, showing terrain, trees, a jetty, translucent water over a visible riverbed, a lit
torch and distance fog; the world, traced, presented from the swapchain. Steady at 13.2 ms a frame
over 10,676 instances at 1994×1366, with the mirror finding the same meshes every frame and retiring
nothing. **The negative test**, asserted at startup on every run: `SDL_GL_GetCurrentContext()` is
null, so nothing under this renderer made a GL context. And the OpenGL path unchanged — five golden
images byte-identical, both test binaries passing, `-DOPENMW_RTX=OFF` still building and linking.

*Not measured*: `plan.md` §12's frame of latency is structurally gone — the present is inside
`renderFrame`, after the trace that made the image — but nobody has put a number on it. The benchmark
corpus needs `-DOPENMW_RTX_BENCH=ON`, which this build does not have.

### Step 5 — the material becomes the only authored form — **withdrawn, and here is the count**

The plan was: `GlRenderer` compiles state sets from `Surface::Material`, the loaders stop building
them, and the equivalence sweep from step 1 makes the swap safe. Counting what a state set actually
holds says otherwise.

`NifOsg` makes **34** distinct writes into a state set — thirteen uniforms, eight attributes, ten
modes, three render-bin selections, two texture attributes. `Shader::ShaderVisitor` adds **24** more,
plus every define and the program itself. `Surface::Material` has **15** fields.

The gap is not detail the description is missing. It is `osg::Depth` and `GL_DEPTH_TEST`,
`osg::PolygonOffset`, three different `setRenderBinDetails`, `osg::FrontFace`'s winding,
`osg::Stencil`'s eight, `osg::PolygonMode` for wireframe, `SceneUtil::TextureType` per unit, the
vertex-colour mode, and uniforms named `useTreeAnim`, `useFalloff`, `falloffParams`, `specStrength`,
`envMapColor`. **Every one of those is rasterizer state and no renderer that traces will ever read
it.** Absorbing them would make `Surface::Material` a state set with different spelling — which its
own comment forbids ("what a surface is, as the content said and before any renderer has an
opinion") and which §9 warns about one layer up.

What is genuinely authored twice is the subset the description already covers: textures by role, the
alpha mode and its threshold, two-sidedness, the four colours. Seven writes out of fifty-eight. And
the drift that duplication risks is the thing step 1's sweep already tests on every run — it walks an
exterior and the densest interior and asserts that every description agrees with the pipeline state
beside it. So the step buys the deletion of a seventh of one duplication that is already guarded,
and pays for it by rewriting how every NIF's rasterizer state is built.

**What was hiding inside this step, and was done instead.** The other half of its last sentence: the
fields the description carries that the *trace* does not read. `Surface::Material` recorded the UV
transform and `NifOsg::UVController` animated it, and `Rtx::Material` had no field for it — so 432
surfaces in Vivec scrolled for the rasterizer and stood still for the ray tracer. Lava, waterfalls,
banners and smoke, all frozen.

`Rtx::Material` carries an `mTextureTransform` now, in the `uv * xy + zw` form the terrain layers
already used, so one sampler helper serves both and the shader gained no branch. The
scale-about-the-middle is resolved on the host — `(uv - 0.5) * scale + 0.5 + offset` becomes
`a = scale, b = 0.5(1 - scale) + offset` — which keeps two multiplies and an add out of every texture
fetch in the frame. It sits on the material rather than the instance because that is what changes:
the same mesh under two controllers is two materials and one geometry, and `SceneDesc::setMaterial`
already existed for shading that moves. Three `UV_IDENTITY` uses in the shader went with it, and the
constant with them.

*Verified by*: two hand-computed tests in the extractor suite, one for a scrolling surface and one
for the identity every other surface has to keep; and the golden images, of which exactly one moved
— Vivec, which is where the 432 are.

### Step 6 — the GUI, then the offscreen views

A `MyGUI::RenderManager` over Vulkan — about 1,500 lines, and a second implementation of MyGUI's own
interface rather than a new abstraction. Then the doll, the race preview and the map tiles as
`OffscreenView`s, which for a ray tracer is a trace into an image.

## 8. What breaks, and the decision for each

| | decision |
|---|---|
| **`PostProcessor`** — 22 call sites over eight files, eleven of them Lua bindings and five the HUD | Gated on `Capabilities::mPostProcessing`. The Lua bindings raise a clear error rather than dereferencing null, and `MWGui::PostProcessorHud` is not registered. The largest diff outside step 3, and there is no way to make it smaller: a renderer with no shader chain cannot pretend to have one |
| **`SkyManager`** | Split. The dome is the rasterizer's; `getSkyColor`, `skyGetMasserPhase`, `getBaseWindSpeed` and `getPrecipitationAlpha` are weather state other systems read, and belong beside `FogManager` |
| **`Water`** | Same shape, smaller. `isUnderwater` and the water level are world state; the reflection, refraction and ripple passes are the rasterizer's |
| **Stereo** | `Capabilities::mStereo`. The settings page hides what a renderer cannot do rather than offering a control that silently does nothing |
| **Shadow settings** | `Capabilities::mShadowMaps`. A ray tracer answers no, and that is correct rather than missing |
| **OSG stats overlay (F3/F4), `ScreenCaptureHandler`** | Rasterizer instrumentation. `Capabilities::mDebugOverlays`; the ray tracer has `Rtx::FrameTimes` and `OPENMW_RTX_BENCH` |
| **The loading screen's frozen background** | A `glCopyTexSubImage` of the last drawn frame. A flat colour where a renderer cannot supply one |

## 9. What this is not

**Not a hardware abstraction layer.** `Renderer` has nineteen methods and none of them is called
more than a few times a frame. The moment a buffer, an image, a command list or a pipeline appears in `renderer.hpp`,
this has become a mini-GL that Vulkan does not fit and Metal fits worse.

**Not a rewrite of the rasterizer.** Steps 2 and 3 move about 14,500 lines and change none of them.
Step 5 changes how state sets are built and is the only step that touches rasterizer behaviour, which
is why it comes after an equivalence test that can prove it did not.

**Not scaffolding.** `plan.md` §3 lists the post-processor, the rasterizer's water and sky,
`osgViewer` and the shadow maps among the deletions, which reads as removing them from the tree.
`CLAUDE.md` says the opposite and gives the reason: *"Keeping it that way is what makes 'does the RT
path do this correctly' answerable by comparison."* **`CLAUDE.md` wins** — the rasterizer is the
reference implementation, and a working one is worth more than the lines it costs. `plan.md` §3's
list should be read as what the ray tracer stops *needing*, not as what leaves the tree. The only
things deleted outright are the ones that exist solely to bridge two renderers into one frame:
`components/rtxgl`, `MWRender::Rtx::Composite`, and every `#ifdef OPENMW_RTX` inside a file upstream
owns.
