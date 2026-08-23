# The GUI, and the pictures inside it

`docs/rtx/renderers.md` §7 step 6 is one paragraph: *"A `MyGUI::RenderManager` over Vulkan — about
1,500 lines, and a second implementation of MyGUI's own interface rather than a new abstraction. Then
the doll, the race preview and the map tiles as `OffscreenView`s, which for a ray tracer is a trace
into an image."*

That paragraph is right about the destination and wrong about the shape, and it hides the harder
half. This document is the investigation and the design that follows from it. It does not overlap
`renderers.md`, which is the seam between the game and a renderer, or `backends.md`, which is Vulkan
against Metal below that seam.

---

## 1. What is actually there

**MyGUI's backend surface is small.** Four interfaces, thirty pure virtual functions between them:
`RenderManager` (8), `ITexture` (14), `IVertexBuffer` (4), `IRenderTarget` (4). The OpenGL
implementation of all of it is `components/myguiplatform`, 1,529 lines — and **most of that is not
about OpenGL.** The name-to-texture map, the view size and its scaling factor, the render-target
info, the vertex buffer's lock/unlock contract, the batching in `doRender`: none of it names a
graphics API. What does is one `osg::Geometry` per buffer, one `osg::Texture2D` per texture, and a
`Drawable` whose `drawImplementation` issues the draw.

**`ITexture::getRenderTarget` returns null and always has.** MyGUI can render a widget tree into a
texture and this backend has never let it, so nothing in OpenMW depends on it. A second backend
inherits that and needs no render pass of its own for layers.

**The Vulkan backend has no graphics pipeline at all.** Everything in `components/rtxvulkan` is
compute — the trace, the denoiser, exposure, tone mapping, the composite. There is no render pass, no
vertex input state, no rasterizer state, no `vkCmdDraw`. A GUI is the first thing that will need one.

**Eleven places in `mwgui` construct `MyGUIPlatform::OSGTexture` directly**, which is the game
reaching past MyGUI's own factory into one renderer's implementation of it. They are not one problem:

| where | what it is |
|---|---|
| `inventorywindow.cpp:100`, `race.cpp:164` | the inventory doll and the race preview — a subtree rendered from a fixed viewpoint |
| `mapwindow.cpp:630,644` | a local map tile and its fog of war, per cell |
| `mapwindow.cpp:1342,1347` | the global map's base and its overlay |
| `videowidget.cpp:53` | frames a video decoder wrote |
| `savegamedialog.cpp:526` | a screenshot read out of a save file |
| `loadingscreen.cpp:306` | a copy of the last frame the rasterizer drew |

Three shapes hide in that list: **something the renderer drew** (the doll, the previews, the map
tiles), **something the game filled with bytes** (video frames, a saved screenshot, the fog of war,
and the global map's base image, which is painted from land data on a work queue), and **the last
frame** (the loading screen's frozen backdrop). Each wants a different answer and only the first is
what step 6's second sentence is about.

**The two maps are one problem, not two.** `GlobalMap::exploreCell` takes the *`osg::Texture2D` of a
local map tile* and blits it into its overlay with a quad and a camera, so a tile that stops being an
`osg::Texture2D` is a tile the global map cannot read. Whatever re-expresses the local map has to
re-express the composite above it in the same step; doing one alone moves the leak from `mwgui` into
`mwrender` rather than closing it.

**`windowmanagerimp.cpp` already does it correctly three times** — `MyGUI::RenderManager::getInstance()
.createTexture("white")` and two siblings. The neutral path exists and is in use; it is the eleven
that went around it.

**The render-to-texture features are 2,445 lines** — `characterpreview`, `localmap`, `globalmap` —
and `renderers.md` §1 already files them as *"game features built as render-to-texture"* rather than
as renderer. What a doll is, how it is posed, which light is on it and what the map paints on top are
the game's. Only *"draw this subtree from here into an image that size"* is not.

## 2. The mistake in the one-paragraph plan

"A `MyGUI::RenderManager` over Vulkan" writes the backend **twice** — once for Vulkan, once for Metal
— and 1,500 lines is the estimate for one of them. But the split in §1 says most of a MyGUI backend
is bookkeeping that no API has an opinion about. Writing it per API would copy the bookkeeping and
then let the copies drift, which is the thing `backends.md` exists to prevent: *"Content, light
transport and what the scene is live in the core, written once. What is true of an API lives in its
backend, written twice, and that cost is paid rather than abstracted away."*

A GUI is content. Only two operations under it are true of an API.

## 3. The design

### 3.1 One MyGUI backend, over the renderer rather than over an API

`components/myguirtx` implements MyGUI's four interfaces against `Rtx::Renderer`. It is written once
and both backends run it. What it needs from below is two things and no more:

```cpp
// components/rtx/renderer.hpp — the whole API-specific surface of a GUI.

/// A quad-list vertex, in the layout MyGUI hands over: screen position, packed colour, uv.
struct GuiVertex { float mX, mY, mZ; std::uint32_t mColour; float mU, mV; };

/// One run of vertices drawn with one texture.
struct GuiBatch { Index mTexture; std::uint32_t mFirst; std::uint32_t mCount; };

class Renderer
{
    /// A texture the GUI draws with, sized once and written whenever it changes.
    ///
    /// Separate from the scene's bindless array: that one is indexed by material and rebuilt when
    /// the world changes, and a font atlas has nothing to do with either.
    virtual Index addGuiTexture(std::uint32_t width, std::uint32_t height) = 0;
    virtual void writeGuiTexture(Index texture, std::span<const std::uint8_t> rgba) = 0;
    virtual void dropGuiTexture(Index texture) = 0;

    /// Everything MyGUI asked to draw this frame, over the finished picture, in one call.
    ///
    /// **After tone mapping and before present.** The GUI's colours are display-referred — MyGUI
    /// picked them looking at a monitor — so putting them through a curve meant for radiance is how
    /// a menu comes out grey.
    virtual void drawGui(std::span<const GuiVertex> vertices, std::span<const GuiBatch> batches) = 0;
};
```

Vulkan implements those with its first graphics pipeline: dynamic rendering into the displayable
image, one vertex buffer, alpha blending, a push descriptor per batch. Metal implements the same
three-and-one with a render command encoder. Neither knows what a widget is.

### 3.2 `MWRender::OffscreenView` for the pictures inside the GUI

```cpp
// apps/openmw/mwrender/offscreenview.hpp

/// A picture of part of the world, taken somewhere other than the eye.
struct OffscreenViewSpec
{
    struct Perspective { float mFieldOfView; };      //< vertical, degrees
    struct Orthographic { float mWidth, mHeight; };  //< world units across

    osg::Node& mScene;           //< the subtree, which the game built and poses
    int mWidth, mHeight;
    unsigned int mMask;          //< vismask.hpp
    std::variant<Perspective, Orthographic> mProjection;
    float mNear, mFar;
    osg::Vec4f mClearColour;
    osg::Vec3f mSunDirection;    //< the only light
    osg::Vec4f mSunDiffuse, mSunAmbient;
    bool mFromWorld;             //< a piece of the world, or a group built for this picture alone
};

class OffscreenView
{
public:
    virtual ~OffscreenView() = default;

    /// Where the picture is taken from, and how much of the image it fills.
    virtual void setView(const osg::Matrixf& view) = 0;
    virtual void setExtent(int width, int height) = 0;

    /// The subtree is not the subtree it was — geometry added or replaced, rather than moved.
    virtual void sceneChanged() = 0;

    /// Draws it again, because what it shows changed. Not per frame: a doll is redrawn when the
    /// player puts something on, and a map tile when the cell is first entered.
    virtual void redraw() = 0;

    /// Also keep the picture in main memory, and that copy — null until the last redraw reaches it.
    virtual void keepCopy() = 0;
    virtual const osg::Image* getCopy() const = 0;

    /// What is at this point of the drawn picture — a ray cast, wearing the rasterizer's clothes.
    virtual bool pick(float x, float y, osg::NodePath& hit) const = 0;

    /// What the GUI shows. Owned here, and a `MyGUI::ITexture` rather than anything of OSG's,
    /// which is the whole point.
    virtual MyGUI::ITexture& getTexture() const = 0;
};
```

`Renderer::createOffscreenView(const OffscreenViewSpec&)` is the factory. The rasterizer answers with
the `osg::Camera` render-to-texture it already has and wraps the result in an `OSGTexture`. The ray
tracer answers with a trace into an image and a GUI texture. **The rendered sites in §1 stop naming a
texture class at all.**

**The spec grew with its callers and not ahead of them.** The projection variant, `mFromWorld` and
the copy all arrived in 6.2 with the local map, which is the caller that needed them; in 6.1 they
would have been three branches nothing reached.

### 3.3 The three shapes, answered

- **Something the renderer drew** — `OffscreenView::getTexture()`. Eight sites.
- **Something the game filled** — `MyGUI::RenderManager::createTexture` and `lock`/`unlock`, which is
  MyGUI's own API and already works on any backend. The video decoder hands pixels instead of an
  `osg::Texture2D`; the save screenshot and the fog of war are already byte buffers wearing an
  `osg::Texture2D`.
- **The last frame** — `Renderer::freezeFrame()`, which hands back a picture rather than being handed
  one to fill. **Both renderers can do this**, which is why it is not a capability: the rasterizer
  copies its framebuffer where it stands, and a renderer that owns its swapchain blits from the image
  it just presented. Filling a texture the GUI made would mean reading either back to main memory and
  handing over pixels — the same picture at several times the price, on the frame a load begins.
  `renderers.md` §8 offers "a flat colour where a renderer cannot supply one" — no renderer here
  cannot.

### 3.4 Where the `if`s would have been

Every branch this could have grown, and why it does not exist:

| the branch | why there is none |
|---|---|
| which `ITexture` class to construct | MyGUI's own factory makes it; the backend behind it is the renderer's |
| which render-to-texture mechanism | `OffscreenView`, made by the renderer that will draw it |
| whether the loading screen can freeze the frame | both can, so nothing asks |
| whether the GUI can be drawn at all | a renderer with no GUI is not a thing this design admits — the one that has none today is a step, not a shape |
| Vulkan or Metal, anywhere above `components/rtx` | the backend split is already below that line and stays there |

The one capability that survives from step 4 is `mTextureUnits`, and this adds none. `#ifdef
OPENMW_RTX` appears in `components/myguirtx`'s own build and nowhere in `apps/openmw`.

### 3.5 What each side owns

```
components/myguiplatform/   MyGUI over OSG. Unchanged; it is the rasterizer's.
components/myguirtx/        MyGUI over Rtx::Renderer. Written once, run by both backends.
components/rtx/             + GuiVertex, GuiBatch, and the four calls in 3.1
components/rtxvulkan/       + GraphicsPipeline, + GuiPass
components/rtxmetal/        + the same two, in Metal
apps/openmw/mwrender/       + offscreenview.hpp, and the RTT features re-expressed over it
```

## 4. Implementation steps

Ordered so each lands, builds and is checkable, and so the rasterizer keeps working throughout.

### Step 6.1 — `OffscreenView`, with only the rasterizer behind it

The interface, `GlRenderer::createOffscreenView` wrapping what `CharacterPreview` already does, and
the inventory doll and the race preview rewritten to ask for one. No Vulkan yet, no behaviour change.

**The spec carries what those two want and no more** — a perspective, a bare subtree, one directional
light, a transparent clear. An orthographic projection and a subtree that is already part of the
world are the local map's, and they arrive in 6.2 with the caller that needs them; writing them a
step early would be a design with one example and two unreachable branches.

*Verified by*: the game under OpenGL, with the doll and the race preview unchanged;
`MyGUIPlatform::OSGTexture` no longer named by either.

### Step 6.2 — the two maps — **done**

`LocalMap` over `OffscreenView`, which grew the spec an orthographic projection, `mFromWorld` and a
main-memory copy. `GlobalMap` over that copy: **the overlay is composed in main memory and nothing
about it is rendered any more.** The render-to-texture it used to go through existed to do one
downscale on the device, and paid for that with a camera per explored cell, a shader pair, a second
copy of the image and a read back to keep the two in step — all of which is gone, along with
`cleanupCameras` on both classes. A box filter over the whole tile is cheaper than that and better
than what it replaced: the device sampled four texels of a picture it was shrinking fourteen-fold.

The one cost added is a read of each exterior tile off the device, on the frame it is drawn. It buys
the removal of the per-cell round trip that used to follow it, and the paint lands two frames after
the cell is entered instead of in the same frame — which nothing was waiting on.

**The map camera also stopped being fitted to the loaded scene.** It hangs at a fixed height and
looks through a fixed slab, so a tile no longer changes because a neighbouring cell arrived, and a
segment keeps one picture and redraws it instead of building a new one. That is what makes the tile
the global map reads a stable thing, and it fixes a standing bug where a re-rendered exterior tile
never reached the widget that was already showing the old one.

**One bug found on the way, and it was not in this code.** `MyGUIPlatform::OSGTexture::lock` sized
its buffer from the texture's *current* dimensions, and `osg::Texture2D::apply` rewrites those to the
nearest power of two unless told not to. Any manually created texture that is not a power of two and
is locked more than once therefore had every upload after the first fill part of a larger buffer and
leave the rest undefined — for a 954×864 overlay, the top 21% blank and the rest skewed. Nothing
upstream locks such a texture twice, so it had never shown. Fixed in `components/myguiplatform`.

*Verified by*: the game under OpenGL, with the local map drawing Balmora as before and the world map
showing both the land and the explored cells over it, loaded from a savegame and added to on the
cell the player entered.

### Step 6.3 — the game stops filling textures through OSG

The video widget, the save screenshot and the fog of war move to
`MyGUI::RenderManager::createTexture` and `lock`/`unlock`. `Renderer::freezeInto` replaces the
loading screen's framebuffer callback.

*Verified by*: a video plays, a save shows its thumbnail, the fog of war reveals, the loading screen
still freezes the frame behind it — all under OpenGL, where every one of them works today.

### Step 6.4 — the first graphics pipeline — **done**

`Rtx::GraphicsPipeline` beside `ComputePipeline`, built the same way and for the same reason — three
handles that fail as one — with what a raster pipeline needs and a compute one does not: two stages,
a vertex layout, a blend, a dynamic viewport, and the format of the image it will draw into.
**No render pass and no framebuffer object**: it is told the format and the recording says which
image, so a resize does not rebuild it.

`GuiPass` over that: a textured, blended triangle list, a push descriptor per batch, loaded over
whatever was in the target rather than clearing it. **The vertex shader is a pass-through** — MyGUI
multiplies widget pixels by the view size itself, so its vertices arrive in clip space, and the one
thing that differs between its clip space and Vulkan's is answered by a flipped viewport.

Two things the writing of it turned up. `GuiVertex` is 24 bytes and not 20 — MyGUI's own layout,
which a backend takes as it finds. And the device had never asked for `dynamicRendering`: this
driver allowed it anyway and the layers said so, which is the whole argument for running the suite
validated.

*Verified by*: four GPU tests in `apps/components_tests/rtx/guipass.cpp`, no window and no MyGUI. A
half-transparent quad over an opaque background lands on exact bytes — an alpha of 128/255 makes the
source's contribution `255 × 128/255 = 128` and what it leaves of the destination `255 × 127/255 =
127`, so there is no rounding to argue about; a two-by-two texture proves which row reaches the top
of the frame, which is what catches a flipped V; two batches prove each is drawn with its own
texture; and an empty frame records nothing. Validation errors fail the tests rather than print.

### Step 6.5 — the GUI textures, and `drawGui` — **done**

The four calls of §3.1 are on `Rtx::Renderer`, with `Rtx::GuiTextures` behind them: a table of
images addressed by slot, where **a slot a texture gave back is taken over before the table grows**,
because a session opens and closes menus for hours and a table that only grew would be a slow leak
with a number on it. A texture is cleared when its slot is made, so it is sampleable from the moment
it exists and the pass never has to ask whether one is ready.

`drawGui` is **its own submit, after the frame's**. The GUI is collected once the world has been
drawn and there is nothing to gain by holding the frame open for it; what it costs is one more queue
submit on the frames the interface is up, which is a number for M12 rather than a reason to fold the
two together now. The target gained `COLOR_ATTACHMENT` usage and is cleared to black in `GENERAL`
when it is sized — **a main menu and a loading screen draw a GUI with no world behind them**, and
before this there was no defined thing for them to draw over.

*Verified by*: seven more GPU tests, none of which needs a window. A batch shows its texture times
its vertex colour; a second call lands over the first, which is what makes a GUI out of one call a
frame; rewriting a texture changes what is drawn with it, which is the whole of what a video frame
needs; a freed slot is reused; an unwritten texture is blank rather than whatever the memory held;
an empty GUI touches nothing. And the last of them traces a wall, draws over half of it, and asserts
that the other half is byte-for-byte what the trace left — the first time the two halves of this
renderer meet.

### Step 6.6 — `components/myguirtx` — **drawn, and one defect short of done**

MyGUI's four interfaces over `Rtx::Renderer`, written once for every backend as §2 argued they
should be. `MyGUIPlatform::Platform` became backend-neutral to carry it: the log and the data
manager are the same whatever draws, so a backend now hands its render manager in already made,
behind a two-method base (`GuiRenderManager`) that adds the `initialise` and `shutdown` MyGUI itself
does not declare. Nothing above the renderers names a concrete backend any more, and
`enableShaders` — which is only the rasterizer's — moved out of `WindowManager` and into
`GlRenderer::createGuiPlatform`.

**Nothing here is driven by a scene graph.** The other backend hangs its frame event on an OSG update
callback and its draw on a cull callback; this one is called by the renderer's own frame — widget
animation from `updateTraversal`, the triangles from `renderFrame` and `renderGui`. That second call
is what puts a GUI on a frame nothing traced.

*Verified by*: the game on the ray tracing path with a savegame — the inventory window, its skin,
tabs, item icons, text and encumbrance bar, the map window, the HUD bars and the crosshair, all over
a traced Balmora, right way up and correctly composited, with the validation layers quiet. The
inventory doll is blank, which is what 6.7 is for.

**Not done, because starting at the main menu crashes.** The ray tracing path takes the process down
on the fifth or sixth frame with a MyGUI widget overwritten by heap corruption. It is recorded in
`.notes/ISSUES.md` with what has been ruled out; the interface it draws is right, and where the
corruption comes from is not yet known.

### Step 6.7 — `RtxRenderer::createOffscreenView`

A trace into an image, at the doll's size, from the doll's viewpoint. The last of §1's eleven.

*Verified by*: the inventory doll and a local map tile, traced.

## 5. What this is not

**Not a GUI abstraction.** MyGUI is the abstraction; there are two implementations of *its*
interface, and the second one is shared by every backend. Nothing in this design invents a widget, a
layout or a draw call of its own.

**Not a portability layer over Vulkan and Metal.** Four functions and two structs cross that line,
and none of them names a buffer, an image, a pipeline or a command list.

**Not a reason to touch the rasterizer's GUI.** `components/myguiplatform` is 1,529 lines that work,
and they stay exactly as they are — the reference implementation, for the same reason `CLAUDE.md`
gives everywhere else.
