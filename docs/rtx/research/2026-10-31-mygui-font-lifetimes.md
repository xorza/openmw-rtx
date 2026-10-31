# What MyGUI 3.4.3 caches of a font, and what a rebuild leaves stale — 2026-10-31

Read from the MyGUI 3.4.3 sources (tag `MyGUI3.4.3`, the version `CMakeLists.txt` requires) and
the fork at `5234767c33`. What `.notes/gui-scale-plan.md` builds on.

## A TrueType font rebuilds itself in place only through `textureInvalidate`

- `ResourceTrueTypeFont::setResolution` and `setSize` only store the value; `initialise` builds the
  atlas from them.
- `initialise` destroys the font's texture and creates a new one under the same name
  (`MyGUI_ResourceTrueTypeFont.cpp:720-727`). The `ITexture*` changes; the `IFont*` does not.
- `createGlyph` fills the glyph table with `insert` (`:932`), which keeps an entry already there.
  A second `initialise` therefore keeps every old glyph's size and UV rectangle and packs the new
  atlas from them.
- `textureInvalidate` clears the glyph table and calls `initialise` (`:371-374`). It is public, and
  it is the one call that rebuilds the font whole.
- `initialise` reads the font file again through `DataManager::getData(mSource)` (`:793`), resolved
  against the data manager's resource path at the time of the call. `Gui::FontLoader` loads with
  that path set to `fonts` and puts it back after (`fontloader.cpp`, `SetOldResourcePath`), so a
  rebuild outside the loader reads from `mygui/` and finds nothing.

## The first build of a font differs from every later one

- The first build creates a glyph for every code point in the font's ranges, a code point the face
  lacks among them, and only then takes that code point out of the character map
  (`MyGUI_ResourceTrueTypeFont.cpp:568-586`). The glyph stays in the glyph table, so
  `getGlyphInfo` answers it with the face's own "not defined" glyph. A later build, which starts
  from the pruned map, makes no such glyph, and the same code point is answered with the font's
  `SubstituteCode`. Measured: after a `textureInvalidate`, the letter A of the default font and the
  mono font sat at another place in the atlas than in a load at the same resolution, and every
  metric was equal.
- With no `TabWidth` stated, the first build sets the tab's width from a space's advance at that
  resolution and keeps it (`:603-604`); a later build at another resolution keeps the first one's.

## A book font follows the frame, not the interface scale

`FontLoader` rasterizes a book font at its resolution times `ScalingLayer::getScaleFactor` of the
book layer into the view, times the raster. The view is the frame over the raster, so the
product is the frame over the book layer: at one frame, a scale change leaves a book font's
resolution where it was, and a resolution change moves it.

## A text sub-widget keeps three things of its font

- `EditText::setFontName` stores the `IFont*`, the font's `ITexture*` and, unless a height was
  set, the font's default height (`MyGUI_EditText.cpp:210-222`). Nothing refreshes them later.
- `EditText::getFontName` dereferences the stored `IFont*` (`:247`).
- `TextView` stores each glyph's UV rectangle and scaled size when the text is laid out, and lays
  it out again only when the text is out of date (`MyGUI_TextView.cpp:206-253`).
- A glyph is drawn at the text's height over the font's default height (`MyGUI_TextView.cpp:211-219`),
  so a raster change leaves a text's size in layout units unchanged once its height is set.
- `EditText` is the only class in MyGUI that holds an `IFont*`. In the fork, `BookPage` holds one
  per style (`StyleImpl::mFont`) and one texture per font in each `TextFormat`
  (`bookpage.cpp:926-1207`).

## Every text in the interface comes from one factory

- `SubWidgetManager` registers `EditText` and `SimpleText` under the category `BasisSkin`
  (`MyGUI_SubWidgetManager.cpp:52-53`); a skin's text is made by name through that factory.
- The fork already replaces factories by name: `Gui::Button`, `Gui::TextBox` and `Gui::EditBox`
  take MyGUI's names (`components/widgets/box.hpp`, `widgets.cpp`), and `PageDisplay` and
  `LuaTileRect` register under `BasisSkin` (`bookpage.cpp:1423`, `lua_ui/util.cpp:30`).

## A render item lets go of a texture once its last text leaves

- `RenderItem::removeDrawItem` sets the item's texture to null when its last draw item goes
  (`MyGUI_RenderItem.cpp:100-104`), and `renderToTarget` skips an item with no texture (`:31`).
- `LayerNode::addToRenderItem` finds an item by comparing texture pointers (`MyGUI_LayerNode.cpp:169`).
- So a text that leaves an item before the next render, after its font's texture was replaced,
  leaves nothing dangling. A text that does not leave keeps the destroyed texture in the item.

## The ray tracer's GUI backend keeps a dropped texture until its frames are done

`myguirtx::Texture::release` drops the slot (`SlotTexture::drop`), and
`GuiTextures::drop` keeps the image on the upload batch until the submit that last used it
(`components/rtxvulkan/gui/guitextures.cpp:144-157`). Replacing a font's texture between frames is
safe for the device.

## Where the interface scale is read, and when

| reader | what it reads | when |
|---|---|---|
| `WindowManager::mRasterScale` | `max(interface scale, setting × display scale)` | once, in the constructor |
| `FontLoader` | the raster scale, `ceil`ed for a font and not for its book font | once, at load |
| `FontLoader` | `ScalingLayer::getScaleFactor(bookSize)`, from the view size | once, at load |
| `LocalMap::mMapResolution` | `local map resolution × getRasterScale()` | once, in the constructor |
| `MouseManager::mGuiCursorX/Y` | the cursor in layout units | on each mouse move |
| `InventoryWindow::getPreviewViewportSize` | widget size × scale | on a window resize |
| `WindowManager::fitCursors` | interface, Lua and display scales | on each `layOut` |
| `WindowManager::layOutInterface` | the scale, the frame | on each `layOut`; moves tracked windows only |

A display change or a resolution change already runs `layOut` while the game runs, and every row
read "once" above is stale after one.

## Measurements

The debug flavour, this machine (i9-13980HX), the shipped fonts of `files/data`, through
`Rtx::Testing::GuiInstance` at a 2560 × 1440 frame:

| raster | `FontLoader::fit` | default font's atlas | mono font's atlas |
|---|---|---|---|
| 1 | 6.1 ms | 256 × 256 | 256 × 256 |
| 2 | 7.0 – 7.5 ms | 512 × 512 | 512 × 256 |
| 3 | 7.9 – 8.2 ms | 1024 × 512 | 512 × 512 |

A fit at one frame rasterizes the three fonts and not their book fonts. Building each font twice
at load put the fixture's construction at 26 ms, from 14 ms.
